/* SM-T311 isolated Exynos G2D: real GEM COPY, SCALE, OVER and CPU reference.
 * All allocations are PRIVATE GEM, never FIMD/HWC/Mali. Runs bounded iterations.
 * Not a system-wide FPS benchmark. EXYNOS_BO_CONTIG necessary without G2D IOMMU.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "exynos_drmif.h"
#include "exynos_fimg2d.h"

#define CHECK(test,label) do { if (!(test)) { fprintf(stderr,"FAIL=%s line=%d\n",label,__LINE__); goto cleanup; } } while (0)
/* G2D scratch BOs are mapped WC/noncached on 32-bit ARM; ordinary
 * compiler fences do not guarantee posted CPU stores reached DMA memory.
 */
static void cpu_dma_store_barrier(void) {
   __asm__ __volatile__("dsb sy" ::: "memory");
}
static uint64_t ns(void) {
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (uint64_t)ts.tv_sec*1000000000ULL+(uint64_t)ts.tv_nsec;
}
static uint32_t expected_pixel(size_t i) {
   return UINT32_C(0xff000000) | (((uint32_t)i * UINT32_C(2654435761)) & UINT32_C(0x00ffffff));
}
static unsigned check_pixels(const uint32_t *pix, unsigned n, int mode, uint32_t solid) {
   unsigned bad=0;
   for(unsigned i=0;i<n;i++) {
      uint32_t exp=(mode==0)?expected_pixel(i):solid;
      if(pix[i]!=exp) { if(bad<3) fprintf(stderr,"PIXEL_MISMATCH at=%u got=0x%08" PRIx32 " expected=0x%08" PRIx32 "\n",i,pix[i],exp);bad++; }
   }
   return bad;
}
static struct g2d_image image(struct exynos_bo *bo, unsigned w, unsigned h) {
   struct g2d_image img={0};
   img.width=w;img.height=h;img.stride=w*4;
   img.color_mode=G2D_COLOR_FMT_ARGB8888|G2D_ORDER_AXRGB;
   img.select_mode=G2D_SELECT_MODE_NORMAL;
   img.buf_type=G2D_IMGBUF_GEM;
   img.bo[0]=bo->handle;
   return img;
}
int main(int argc,char **argv) {
   unsigned w=256,h=256,iters=12; int rc=8;
   int fd=-1;struct exynos_device *dev=NULL;
   struct exynos_bo *src=NULL,*dst=NULL; struct g2d_context *ctx=NULL;
   uint32_t *s=NULL,*d=NULL,*reference=NULL;
   const char *mode=(argc>=5)?argv[4]:"";
   const char *submode=(argc==6)?argv[5]:"";
   int cpu_only=!strcmp(mode,"cpu-only");
   int use_kmap=!strcmp(mode,"coherent-kmap") || !strcmp(submode,"coherent-kmap");
   int coherent=use_kmap || !strcmp(mode,"coherent") || !strcmp(submode,"coherent");
   int valid_mode=(argc==4) ||
       (argc==5 && (cpu_only || coherent)) ||
       (argc==6 && cpu_only && coherent);
   if (!valid_mode || sscanf(argv[1],"%u",&w)!=1 || sscanf(argv[2],"%u",&h)!=1 || sscanf(argv[3],"%u",&iters)!=1 || w<32 || h<32 || (w&1) || (h&1) || w>800 || h>1280 || iters<1 || iters>20) {
      fprintf(stderr,"USAGE=%s width_even[32..800] height_even[32..1280] iterations[1..20]\n",argv[0]);return 2;
   }
   size_t pix=(size_t)w*h,bytes=pix*4;
   printf("G2D_PRIVATE_ONLY=%ux%u bytes_per_buffer=%zu iters=%u coherent=%d kmap=%d cpu_only=%d\n",w,h,bytes,iters,coherent,use_kmap,cpu_only);
   fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);CHECK(fd>=0,"card0");
   dev=exynos_device_create(fd);CHECK(dev,"exynos_device_create");
   /* This exact deployed SM-T311 kernel supports both board GEM flags:
    * 0x08 coherent and 0x10 CPU kernel mapping, giving validated 0x18.
    * Unlike 0x00/0x08, 0x18 passed earlier FIMC DMA coherency A/B.
    */
   uint32_t bo_flags=EXYNOS_BO_CONTIG | (coherent ? (1u<<3) : 0) | (use_kmap ? (1u<<4) : 0);
   src=exynos_bo_create(dev,bytes,bo_flags);CHECK(src,"source_cma");
   dst=exynos_bo_create(dev,bytes,bo_flags);CHECK(dst,"dest_cma");
   s=exynos_bo_map(src);d=exynos_bo_map(dst);CHECK(s && d,"map");
   ctx=g2d_init(fd);CHECK(ctx,"g2d_init");
   struct g2d_image a=image(src,w,h),b=image(dst,w,h);
   for(size_t i=0;i<pix;i++)s[i]=expected_pixel(i);
   memset(d,0,bytes);
   cpu_dma_store_barrier();
   unsigned bad_src=check_pixels(s,(unsigned)pix,0,0);
   printf("CPU_SOURCE_AFTER_INIT_ERRORS=%u\n",bad_src);
   CHECK(bad_src==0,"cpu_source_initialization");
   uint64_t start=ns();
   for(unsigned j=0;j<iters;j++)memcpy(d,s,bytes);
   uint64_t cpu_total=ns()-start;
   cpu_dma_store_barrier();
   unsigned bad=check_pixels(d,(unsigned)pix,0,0);
   unsigned bad_src_post=check_pixels(s,(unsigned)pix,0,0);
   printf("CPU_MEMCPY_US_PER_CALL=%.2f errors=%u src_errors_after_copy=%u\n",(double)cpu_total/(1000.0*iters),bad,bad_src_post);
   CHECK(bad==0,"cpu_copy_pixels");
   if(cpu_only){printf("CPU_ONLY_GEM_COPY_WITH_DSB=PASS\n");rc=0;goto cleanup;}
   memset(d,0,bytes);
   cpu_dma_store_barrier();
   start=ns();
   for(unsigned j=0;j<iters;j++) {
      CHECK(g2d_copy(ctx,&a,&b,0,0,0,0,w,h)==0,"g2d_copy_enqueue");
      CHECK(g2d_exec(ctx)==0,"g2d_copy_exec");
   }
   uint64_t gpu_copy_ns=ns()-start;
   cpu_dma_store_barrier();
   bad=check_pixels(d,(unsigned)pix,0,0);
   printf("G2D_COPY_US_PER_CALL=%.2f errors=%u\n",(double)gpu_copy_ns/(1000.0*iters),bad);
   CHECK(bad==0,"g2d_copy_pixels");
   /* Constant image down to quarter source dimensions, scale it 2x in both axes.
    * A separate small logical image is backed by the SAME private (large) BO.
    * DMA never exceeds allocation; w/2 stride contiguous valid.
    */
   const uint32_t solid=UINT32_C(0xff4365a7);
   for(size_t i=0;i<pix/4;i++)s[i]=solid;
   memset(d,0,bytes);
   cpu_dma_store_barrier();
   a=image(src,w/2,h/2);
   b=image(dst,w,h);
   start=ns();
   CHECK(g2d_copy_with_scale(ctx,&a,&b,0,0,w/2,h/2,0,0,w,h,0)==0,"g2d_scale_enqueue");
   CHECK(g2d_exec(ctx)==0,"g2d_scale_exec");
   uint64_t scale_ns=ns()-start;
   cpu_dma_store_barrier();
   bad=check_pixels(d,(unsigned)pix,1,solid);
   printf("G2D_SCALE_2X_US=%.2f errors=%u\n",(double)scale_ns/1000.0,bad);
   CHECK(bad==0,"g2d_scale_pixels");
   /* Blending OVER with opaque source must be exactly source, independent
    * of premultiplication convention. Explicitly test full blend path.
    */
   a=image(src,w,h);
   for(size_t i=0;i<pix;i++)s[i]=UINT32_C(0xff267ad9);
   for(size_t i=0;i<pix;i++)d[i]=UINT32_C(0xff103050);
   cpu_dma_store_barrier();
   start=ns();
   CHECK(g2d_blend(ctx,&a,&b,0,0,0,0,w,h,G2D_OP_OVER)==0,"g2d_blend_enqueue");
   CHECK(g2d_exec(ctx)==0,"g2d_blend_exec");
   uint64_t blend_ns=ns()-start;
   cpu_dma_store_barrier();
   bad=check_pixels(d,(unsigned)pix,1,UINT32_C(0xff267ad9));
   printf("G2D_BLEND_OVER_OPAQUE_US=%.2f errors=%u first=0x%08" PRIx32 "\n",(double)blend_ns/1000.0,bad,d[0]);
   CHECK(bad==0,"g2d_blend_pixels");
   /* Non-square BO with two distinct origin offsets. The request extends
    * outside BOTH destination edges, so exactly the clipped rectangle must
    * change. Every other pixel is a sentinel and must remain unmodified.
    */
   reference=malloc(bytes);CHECK(reference,"cpu_reference_alloc");
   if(w>=96 && h>=128) {
      const unsigned csx=w/4,csy=h/4,csw=w-csx,csh=h-csy;
      for(size_t i=0;i<pix;i++)s[i]=expected_pixel(i);
      memset(d,0,bytes);
      a=image(src,w,h);b=image(dst,w,h);
      cpu_dma_store_barrier();
      CHECK(g2d_copy_with_scale(ctx,&a,&b,csx,csy,csw,csh,0,0,w,h,0)==0,"scale_clip_reference_enqueue");
      CHECK(g2d_exec(ctx)==0,"scale_clip_reference_exec");
      cpu_dma_store_barrier();
      memcpy(reference,d,bytes);
      memset(d,0,bytes);
      cpu_dma_store_barrier();
      CHECK(g2d_copy_with_scale(ctx,&a,&b,csx,csy,csw+71,csh+71,0,0,w,h,0)==0,"scale_clip_equivalent_enqueue");
      CHECK(g2d_exec(ctx)==0,"scale_clip_equivalent_exec");
      cpu_dma_store_barrier();
      CHECK(memcmp(reference,d,bytes)==0,"scale_clip_pixel_equivalence");
      CHECK(g2d_copy_with_scale(ctx,&a,&b,w,0,4,4,0,0,w,h,0)==-EINVAL,"scale_reject_out_of_bounds_origin");
      CHECK(g2d_copy_with_scale(ctx,&a,&b,0,0,1,1,0,0,0,1,0)==-EINVAL,"scale_reject_zero_destination");
      memset(d,0,bytes);
      cpu_dma_store_barrier();
      CHECK(g2d_scale_and_blend(ctx,&a,&b,csx,csy,csw,csh,0,0,w,h,G2D_OP_OVER)==0,"blend_clip_reference_enqueue");
      CHECK(g2d_exec(ctx)==0,"blend_clip_reference_exec");
      cpu_dma_store_barrier();
      memcpy(reference,d,bytes);
      memset(d,0,bytes);
      cpu_dma_store_barrier();
      CHECK(g2d_scale_and_blend(ctx,&a,&b,csx,csy,csw+71,csh+71,0,0,w,h,G2D_OP_OVER)==0,"blend_clip_equivalent_enqueue");
      CHECK(g2d_exec(ctx)==0,"blend_clip_equivalent_exec");
      cpu_dma_store_barrier();
      CHECK(memcmp(reference,d,bytes)==0,"blend_clip_pixel_equivalence");
      CHECK(g2d_scale_and_blend(ctx,&a,&b,w,0,4,4,0,0,w,h,G2D_OP_OVER)==-EINVAL,"blend_scale_reject_out_of_bounds_origin");
      CHECK(g2d_scale_and_blend(ctx,&a,&b,0,0,1,1,0,0,0,1,G2D_OP_OVER)==-EINVAL,"blend_scale_reject_zero_destination");
      printf("G2D_REAL_SCALE_AND_BLEND_CLIP_EQUIVALENCE=PASS\n");
      const unsigned sx=w-37,sy=h-71,dx=w-29,dy=h-49;
      const unsigned cw=29,ch=49;
      const uint32_t sentinel=UINT32_C(0xff123456);
      for(size_t i=0;i<pix;i++)s[i]=expected_pixel(i);
      for(size_t i=0;i<pix;i++)d[i]=sentinel;
      cpu_dma_store_barrier();
      a=image(src,w,h);b=image(dst,w,h);
      CHECK(g2d_copy(ctx,&a,&b,sx,sy,dx,dy,90,110)==0,"g2d_copy_clipped_enqueue");
      CHECK(g2d_exec(ctx)==0,"g2d_copy_clipped_exec");
      cpu_dma_store_barrier();
      bad=0;
      for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
         uint32_t val=sentinel;
         if(x>=dx && x<dx+cw && y>=dy && y<dy+ch)
            val=s[(size_t)(sy+y-dy)*w+(sx+x-dx)];
         if(d[(size_t)y*w+x]!=val) {if(bad<3)fprintf(stderr,"CLIPPED_COPY_MISMATCH x=%u y=%u got=%08" PRIx32 " expect=%08" PRIx32 "\n",x,y,d[(size_t)y*w+x],val);bad++;}
      }
      printf("G2D_CLIPPED_COPY_ERRORS=%u area=%ux%u\n",bad,cw,ch);
      CHECK(bad==0,"g2d_copy_clipped_pixels");
      CHECK(g2d_copy(ctx,&a,&b,w,1,0,0,1,1)==-EINVAL,"copy_reject_bad_x_origin");
      CHECK(g2d_copy(ctx,&a,&b,0,h,0,0,1,1)==-EINVAL,"copy_reject_bad_y_origin");
      CHECK(g2d_copy(ctx,&a,&b,0,0,0,0,0,1)==-EINVAL,"copy_reject_zero_width");
      CHECK(g2d_blend(ctx,&a,&b,w,1,0,0,1,1,G2D_OP_OVER)==-EINVAL,"blend_reject_bad_origin");
      /* Real overlapping 2D memmove; compare the entire BO against an
       * unmodified CPU snapshot, including all untouched edges.
       */
      for(size_t i=0;i<pix;i++)s[i]=expected_pixel(i);
      memcpy(reference,s,bytes);
      a=image(src,w,h);
      cpu_dma_store_barrier();
      const unsigned mx=3,my=5,tx=9,ty=11,mw=w-24,mh=h-24;
      CHECK(g2d_move(ctx,&a,mx,my,tx,ty,mw,mh)==0,"g2d_move_overlap_enqueue");
      CHECK(g2d_exec(ctx)==0,"g2d_move_overlap_exec");
      cpu_dma_store_barrier();
      bad=0;
      for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
         uint32_t val=reference[(size_t)y*w+x];
         if(x>=tx && x<tx+mw && y>=ty && y<ty+mh)
            val=reference[(size_t)(my+y-ty)*w+(mx+x-tx)];
         if(s[(size_t)y*w+x]!=val) {if(bad<3)fprintf(stderr,"MOVE_OVERLAP_MISMATCH x=%u y=%u got=%08" PRIx32 " expect=%08" PRIx32 "\n",x,y,s[(size_t)y*w+x],val);bad++;}
      }
      printf("G2D_OVERLAP_MOVE_ERRORS=%u area=%ux%u\n",bad,mw,mh);
      CHECK(bad==0,"g2d_move_overlap_pixels");
      CHECK(g2d_move(ctx,&a,w,0,0,0,1,1)==-EINVAL,"move_reject_bad_origin");
      printf("G2D_COPY_MOVE_BLEND_BOUNDS_TESTS=PASS\n");
   }
   printf("ALL_HARDWARE_COPY_SCALE_BLEND_PIXEL_TESTS=PASS\n");
   rc=0;
cleanup:
   free(reference);
   if(ctx)g2d_fini(ctx);
   if(dst)exynos_bo_destroy(dst);
   if(src)exynos_bo_destroy(src);
   if(dev)exynos_device_destroy(dev);
   if(fd>=0)close(fd);
   return rc;
}
