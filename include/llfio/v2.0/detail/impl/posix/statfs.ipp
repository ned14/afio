/* Information about the volume storing a file
(C) 2016-2020 Niall Douglas <http://www.nedproductions.biz/> (5 commits)
File Created: Jan 2016


Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License in the accompanying file
Licence.txt or at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


Distributed under the Boost Software License, Version 1.0.
    (See accompanying file Licence.txt or copy at
          http://www.boost.org/LICENSE_1_0.txt)
*/

#include "../../../handle.hpp"
#include "../../../statfs.hpp"

#include <chrono>
#include <mutex>
#include <vector>

#include <sys/mount.h>
#ifdef __linux__
#include <mntent.h>
#include <sys/statfs.h>
#include <sys/sysmacros.h>
#endif

LLFIO_V2_NAMESPACE_BEGIN

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<size_t> statfs_t::fill(const handle &h, statfs_t::want wanted) noexcept
{
  size_t ret = 0;
#ifdef __linux__
  if(!!(wanted & ~(want::iosinprogress | want::iosbusytime)))
  {
    struct statfs64 s
    {
    };
    memset(&s, 0, sizeof(s));
    if(-1 == fstatfs64(h.native_handle().fd, &s))
    {
      return posix_error();
    }
    if(!!(wanted & want::bsize))
    {
      f_bsize = s.f_bsize;
      ++ret;
    }
    if(!!(wanted & want::iosize))
    {
      f_iosize = s.f_frsize;
      ++ret;
    }
    if(!!(wanted & want::blocks))
    {
      f_blocks = s.f_blocks;
      ++ret;
    }
    if(!!(wanted & want::bfree))
    {
      f_bfree = s.f_bfree;
      ++ret;
    }
    if(!!(wanted & want::bavail))
    {
      f_bavail = s.f_bavail;
      ++ret;
    }
    if(!!(wanted & want::files))
    {
      f_files = s.f_files;
      ++ret;
    }
    if(!!(wanted & want::ffree))
    {
      f_ffree = s.f_ffree;
      ++ret;
    }
    if(!!(wanted & want::namemax))
    {
      f_namemax = s.f_namelen;
      ++ret;
    }
    //            if(!!(wanted&&want::owner))       { f_owner      =s.f_owner;  ++ret; }
    if(!!(wanted & want::fsid))
    {
      f_fsid[0] = static_cast<unsigned>(s.f_fsid.__val[0]);
      f_fsid[1] = static_cast<unsigned>(s.f_fsid.__val[1]);
      ++ret;
    }
    if(!!(wanted & want::flags) || !!(wanted & want::fstypename) || !!(wanted & want::mntfromname) || !!(wanted & want::mntonname))
    {
      try
      {
        struct mountentry
        {
          std::string mnt_fsname, mnt_dir, mnt_type, mnt_opts;
          mountentry(const char *a, const char *b, const char *c, const char *d)
              : mnt_fsname(a)
              , mnt_dir(b)
              , mnt_type(c)
              , mnt_opts(d)
          {
          }
        };
        std::vector<std::pair<mountentry, struct statfs64>> mountentries;
        {
          // Need to parse mount options on Linux
          FILE *mtab = setmntent("/etc/mtab", "r");
          if(mtab == nullptr)
          {
            mtab = setmntent("/proc/mounts", "r");
          }
          if(mtab == nullptr)
          {
            return posix_error();
          }
          auto unmtab = make_scope_exit([mtab]() noexcept { endmntent(mtab); });
          struct mntent m
          {
          };
          char buffer[32768];
          while(getmntent_r(mtab, &m, buffer, sizeof(buffer)) != nullptr)
          {
            struct statfs64 temp
            {
            };
            memset(&temp, 0, sizeof(temp));
            // std::cout << m.mnt_fsname << "," << m.mnt_dir << "," << m.mnt_type << "," << m.mnt_opts << std::endl;
            if(0 == statfs64(m.mnt_dir, &temp))
            {
              // std::cout << "   " << temp.f_fsid.__val[0] << temp.f_fsid.__val[1] << " =? " << s.f_fsid.__val[0] << s.f_fsid.__val[1] << std::endl;
              if(temp.f_type == s.f_type && (memcmp(&temp.f_fsid, &s.f_fsid, sizeof(s.f_fsid)) == 0))
              {
                mountentries.emplace_back(mountentry(m.mnt_fsname, m.mnt_dir, m.mnt_type, m.mnt_opts), temp);
              }
            }
          }
        }
#ifndef LLFIO_COMPILING_FOR_GCOV
        if(mountentries.empty())
        {
          return errc::no_such_file_or_directory;
        }
        /* Choose the mount entry with the most closely matching statfs. You can't choose
        exclusively based on mount point because of bind mounts. Note that we have a
        particular problem in Docker:

        rootfs    /          rootfs  rw                       0 0
        overlay   /          overlay rw,relatime,lowerdir=... 0 0
        /dev/sda3 /etc       xfs     rw,relatime,...          0 0
        tmpfs     /dev       tmpfs   rw,nosuid,...            0 0
        tmpfs     /proc/acpi tmpfs   rw,nosuid,...            0 0
        ...

        If f_type and f_fsid are identical for the statfs for the mount as for our file,
        then you will get multiple mountentries, and there is no obvious way of
        disambiguating them. What we do is match mount based on the longest match
        of the mount point with the current path of our file.
        */
        if(mountentries.size() > 1)
        {
          OUTCOME_TRY(auto &&currentfilepath_, h.current_path());
          string_view currentfilepath(currentfilepath_.native());
          std::vector<std::pair<size_t, size_t>> scores(mountentries.size());
          // std::cout << "*** For matching mount entries to file with path " << currentfilepath << ":\n";
          for(size_t n = 0; n < mountentries.size(); n++)
          {
            scores[n].first =
            (currentfilepath.substr(0, mountentries[n].first.mnt_dir.size()) == mountentries[n].first.mnt_dir) ? mountentries[n].first.mnt_dir.size() : 0;
            // std::cout << "***    Mount entry " << mountentries[n].first.mnt_dir << " get score " << scores[n].first << std::endl;
            scores[n].second = n;
          }
          std::sort(scores.begin(), scores.end());
          // Choose the item whose mnt_dir most matched the current path for our file.
          auto temp(std::move(mountentries[scores.back().second]));
          mountentries.clear();
          mountentries.push_back(std::move(temp));
        }
#endif
        if(!!(wanted & want::flags))
        {
          f_flags.rdonly = static_cast<uint32_t>((s.f_flags & MS_RDONLY) != 0);
          f_flags.noexec = static_cast<uint32_t>((s.f_flags & MS_NOEXEC) != 0);
          f_flags.nosuid = static_cast<uint32_t>((s.f_flags & MS_NOSUID) != 0);
          f_flags.acls = static_cast<uint32_t>(std::string::npos != mountentries.front().first.mnt_opts.find("acl") &&
                                               std::string::npos == mountentries.front().first.mnt_opts.find("noacl"));
          f_flags.xattr = static_cast<uint32_t>(std::string::npos != mountentries.front().first.mnt_opts.find("xattr") &&
                                                std::string::npos == mountentries.front().first.mnt_opts.find("nouser_xattr"));
          //                out.f_flags.compression=0;
          // Those filing systems supporting FALLOC_FL_PUNCH_HOLE
          f_flags.extents = static_cast<uint32_t>(mountentries.front().first.mnt_type == "btrfs" || mountentries.front().first.mnt_type == "ext4" ||
                                                  mountentries.front().first.mnt_type == "xfs" || mountentries.front().first.mnt_type == "tmpfs");
          ++ret;
        }
        if(!!(wanted & want::fstypename))
        {
          f_fstypename = mountentries.front().first.mnt_type;
          ++ret;
        }
        if(!!(wanted & want::mntfromname))
        {
          f_mntfromname = mountentries.front().first.mnt_fsname;
          ++ret;
        }
        if(!!(wanted & want::mntonname))
        {
          f_mntonname = mountentries.front().first.mnt_dir;
          ++ret;
        }
      }
      catch(...)
      {
        return error_from_exception();
      }
    }
  }
#else
  struct statfs s;
  if(-1 == fstatfs(h.native_handle().fd, &s))
    return posix_error();
  if(!!(wanted & want::flags))
  {
    f_flags.rdonly = !!(s.f_flags & MNT_RDONLY);
    f_flags.noexec = !!(s.f_flags & MNT_NOEXEC);
    f_flags.nosuid = !!(s.f_flags & MNT_NOSUID);
    f_flags.acls = 0;
#if defined(MNT_ACLS) && defined(MNT_NFS4ACLS)
    f_flags.acls = !!(s.f_flags & (MNT_ACLS | MNT_NFS4ACLS));
#endif
    f_flags.xattr = 1;  // UFS and ZFS support xattr. TODO FIXME actually calculate this, zfs get xattr <f_mntfromname> would do it.
    f_flags.compression = !strcmp(s.f_fstypename, "zfs");
    f_flags.extents = !strcmp(s.f_fstypename, "ufs") || !strcmp(s.f_fstypename, "zfs");
    ++ret;
  }
  if(!!(wanted & want::bsize))
  {
    f_bsize = s.f_bsize;
    ++ret;
  }
  if(!!(wanted & want::iosize))
  {
    f_iosize = s.f_iosize;
    ++ret;
  }
  if(!!(wanted & want::blocks))
  {
    f_blocks = s.f_blocks;
    ++ret;
  }
  if(!!(wanted & want::bfree))
  {
    f_bfree = s.f_bfree;
    ++ret;
  }
  if(!!(wanted & want::bavail))
  {
    f_bavail = s.f_bavail;
    ++ret;
  }
  if(!!(wanted & want::files))
  {
    f_files = s.f_files;
    ++ret;
  }
  if(!!(wanted & want::ffree))
  {
    f_ffree = s.f_ffree;
    ++ret;
  }
#ifdef __APPLE__
  if(!!(wanted & want::namemax))
  {
    f_namemax = 255;
    ++ret;
  }
#else
  if(!!(wanted & want::namemax))
  {
    f_namemax = s.f_namemax;
    ++ret;
  }
#endif
  if(!!(wanted & want::owner))
  {
    f_owner = s.f_owner;
    ++ret;
  }
  if(!!(wanted & want::fsid))
  {
    f_fsid[0] = (unsigned) s.f_fsid.val[0];
    f_fsid[1] = (unsigned) s.f_fsid.val[1];
    ++ret;
  }
  if(!!(wanted & want::fstypename))
  {
    f_fstypename = s.f_fstypename;
    ++ret;
  }
  if(!!(wanted & want::mntfromname))
  {
    f_mntfromname = s.f_mntfromname;
    ++ret;
  }
  if(!!(wanted & want::mntonname))
  {
    f_mntonname = s.f_mntonname;
    ++ret;
  }
#endif
  if(!!(wanted & want::iosinprogress) || !!(wanted & want::iosbusytime))
  {
    OUTCOME_TRY(auto &&ios, _fill_ios(h, f_mntfromname));
    if(!!(wanted & want::iosinprogress))
    {
      f_iosinprogress = ios.first;
      ++ret;
    }
    if(!!(wanted & want::iosbusytime))
    {
      f_iosbusytime = ios.second;
      ++ret;
    }
  }
  return ret;
}

/******************************************* statfs_t ************************************************/

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::pair<uint32_t, float>> statfs_t::_fill_ios(const handle &h, const std::string & /*unused*/) noexcept
{
  (void) h;
  try
  {
#ifdef __linux__
    struct stat s
    {
    };
    memset(&s, 0, sizeof(s));

    if(-1 == ::fstat(h.native_handle().fd, &s))
    {
      if(!h.is_symlink() || EBADF != errno)
      {
        return posix_error();
      }
      // This is a hack, but symlink_handle includes this first so there is a chicken and egg dependency problem
      OUTCOME_TRY(detail::stat_from_symlink(s, h));
    }

    static struct last_reading_t
    {
      struct item
      {
        dev_t st_dev;
        size_t millis{0};
        std::chrono::steady_clock::time_point last_updated;

        uint32_t f_iosinprogress{0};
        float f_iosbusytime{0};
      };
      std::mutex lock;
      std::vector<item> items;
    } last_reading;
    auto now = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> g(last_reading.lock);
      for(auto &i : last_reading.items)
      {
        if(i.st_dev == s.st_dev)
        {
          if(std::chrono::duration_cast<std::chrono::milliseconds>(now - i.last_updated) < std::chrono::milliseconds(100))
          {
            return {i.f_iosinprogress, i.f_iosbusytime};  // exit with old readings
          }
          break;
        }
      }
    }
    try
    {
      int fd = ::open("/proc/diskstats", O_RDONLY);
      if(fd >= 0)
      {
        std::string diskstats;
        diskstats.resize(4096);
        for(;;)
        {
          auto read = ::read(fd, (char *) diskstats.data(), diskstats.size());
          if(read < 0)
          {
            return posix_error();
          }
          if(read < (ssize_t) diskstats.size())
          {
            ::close(fd);
            diskstats.resize(read);
            break;
          }
          try
          {
            diskstats.resize(diskstats.size() << 1);
          }
          catch(...)
          {
            ::close(fd);
            throw;
          }
        }
        /* Format is (https://www.kernel.org/doc/Documentation/iostats.txt):
        <dev id major> <dev id minor> <device name> 01 02 03 04 05 06 07 08 09 10  ...

        Field 9 is i/o's currently in progress.
        Field 10 is milliseconds spent doing i/o (cumulative).
        */
        auto match_line = [&](string_view sv) {
          int major = 0, minor = 0;
          sscanf(sv.data(), "%d %d", &major, &minor);
          // printf("Does %d,%d match %d,%d?\n", major, minor, major(s.st_dev), minor(s.st_dev));
          return (makedev(major, minor) == s.st_dev);
        };
        for(size_t is = 0, ie = diskstats.find(10); ie != diskstats.npos; is = ie + 1, ie = diskstats.find(10, is))
        {
          auto sv = string_view(diskstats).substr(is, ie - is);
          if(match_line(sv))
          {
            int major = 0, minor = 0;
            char devicename[64];
            size_t fields[12];
            sscanf(sv.data(), "%d %d %s %*u %*u %*u %*u %*u %*u %*u %*u %zu %zu", &major, &minor, devicename, fields + 8, fields + 9);
            std::lock_guard<std::mutex> g(last_reading.lock);
            auto it = last_reading.items.begin();
            for(; it != last_reading.items.end(); ++it)
            {
              if(it->st_dev == s.st_dev)
              {
                break;
              }
            }
            if(it == last_reading.items.end())
            {
              last_reading.items.emplace_back();
              it = --last_reading.items.end();
              it->st_dev = s.st_dev;
              it->millis = fields[9];
            }
            else
            {
              auto timediff = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->last_updated);
              it->f_iosbusytime = std::min((float) ((double) (fields[9] - it->millis) / timediff.count()), 1.0f);
              it->millis = fields[9];
            }
            it->f_iosinprogress = (uint32_t) fields[8];
            it->last_updated = now;
            return {it->f_iosinprogress, it->f_iosbusytime};
          }
        }
        // It's totally possible that the dev_t reported by stat()
        // does not appear in /proc/diskstats, if this occurs then
        // return all bits one to indicate soft failure.
      }
    }
    catch(...)
    {
      return error_from_exception();
    }
#else
    /* On FreeBSD, want::iosinprogress and want::iosbusytime could be implemented
    using libdevstat. See https://www.freebsd.org/cgi/man.cgi?query=devstat&sektion=3.
    Code donations welcome!

    On Mac OS, getting the current i/o wait time appears to be privileged only?
    */
#endif
    return {-1, detail::constexpr_float_allbits_set_nan()};
  }
  catch(...)
  {
    return error_from_exception();
  }
}

LLFIO_V2_NAMESPACE_END
