#include <fstab/fstab.h>
#include <stdio.h>
#include <string>
int main(int argc, char** argv) {
  const char *path=argc>1?argv[1]:"/vendor/etc/fstab.lt013g";
  android::fs_mgr::Fstab entries;
  const bool ok=android::fs_mgr::ReadFstabFromFile(path,&entries);
  printf("T311_G558_FSTAB_PROBE ok=%d entries=%zu path=%s\n",ok,entries.size(),path);
  for(const auto& r:entries)
    printf("T311_ENTRY type=%s point=%s zram_size=%lld swap_prio=%d streams=%d wait=%d block=%s\n",r.fs_type.c_str(),r.mount_point.c_str(),(long long)r.zram_size,r.swap_prio,r.max_comp_streams,(int)r.fs_mgr_flags.wait,r.blk_device.c_str());
  return ok?0:2;
}
