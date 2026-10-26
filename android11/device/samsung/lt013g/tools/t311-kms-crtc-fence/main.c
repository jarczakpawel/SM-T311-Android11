#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int main(void) {
  int fd=open("/dev/dri/card0",O_RDONLY|O_CLOEXEC);
  if(fd<0) return perror("card0"),2;
  int cap=drmSetClientCap(fd,DRM_CLIENT_CAP_ATOMIC,1);
  printf("ATOMIC_CLIENT_CAP=%d\n",cap);
  drmModeResPtr r=drmModeGetResources(fd);
  if(!r){perror("resources");close(fd);return 3;}
  int count=0;
  for(int i=0;i<r->count_crtcs;i++){
    const uint32_t id=r->crtcs[i];
    drmModeObjectPropertiesPtr props=drmModeObjectGetProperties(fd,id,DRM_MODE_OBJECT_CRTC);
    if(!props){printf("CRTC=%u PROPERTIES_UNAVAILABLE=%d\n",id,errno);continue;}
    printf("CRTC=%u COUNT=%u\n",id,props->count_props);
    int found=0;
    for(uint32_t j=0;j<props->count_props;j++){
      drmModePropertyPtr prop=drmModeGetProperty(fd,props->props[j]);
      if(!prop)continue;
      if(!strcmp(prop->name,"OUT_FENCE_PTR")){
        printf("OUT_FENCE_PTR_CRTC=%u PROP_ID=%u VALUE=%" PRIu64 " FLAGS=0x%x\n",
          id,prop->prop_id,props->prop_values[j],prop->flags);
        found=1;count++;
      }
      drmModeFreeProperty(prop);
    }
    printf("CRTC_OUT_FENCE_PRESENT=%d\n",found);
    drmModeFreeObjectProperties(props);
  }
  printf("PHYSICAL_KMS_OUT_FENCE_CRTC_COUNT=%d\n",count);
  drmModeFreeResources(r);
  close(fd);
  return count>0?0:4;
}
