#define CL_HPP_TARGET_OPENCL_VERSION 300

#include "utils/bachUtil.h"

int identifySStamps(std::vector<Stamp>& stamps, Image& image) {
  std::cout << "Identifying sub-stamps in " << image.name << "..." << std::endl;

  int index = 0, hasSStamps = 0;
  for(auto& s : stamps) {
    calcStats(s, image);
    findSStamps(s, image, index);
    if(!s.subStamps.empty()) hasSStamps++;
    index++;
  }
  return hasSStamps;
}

void createStamps(Image& img, std::vector<Stamp>& stamps, int w, int h) {
  for(int j = 0; j < args.stampsy; j++) {
    for(int i = 0; i < args.stampsx; i++) {
      int stampw = w / args.stampsx;
      int stamph = h / args.stampsy;
      int startx = i * stampw;
      int starty = j * stamph;
      int stopx = startx + stampw;
      int stopy = starty + stamph;

      if(i == args.stampsx - 1) {
        stopx = w;
        stampw = stopx - startx;
      }

      if(j == args.stampsy - 1) {
        stopy = h;
        stamph = stopy - starty;
      }

      Stamp tmpS{};
      for(int y = 0; y < stamph; y++) {
        for(int x = 0; x < stampw; x++) {
          cl_double tmp = img[(startx + x) + ((starty + y) * w)];
          tmpS.data.push_back(tmp);
        }
      }

      tmpS.coords = std::make_pair(startx, starty);
      tmpS.size = std::make_pair(stampw, stamph);
      stamps.push_back(tmpS);
    }
  }
}

double checkSStamp(SubStamp& sstamp, Image& image, Stamp& stamp) {
  double retVal = 0.0;
  for(int y = sstamp.imageCoords.second - args.hSStampWidth;
      y < sstamp.imageCoords.second + args.hSStampWidth; y++) {
    if(y < 0 || y >= image.axis.second) continue;
    for(int x = sstamp.imageCoords.first - args.hSStampWidth;
        x < sstamp.imageCoords.first + args.hSStampWidth; x++) {
      if(x < 0 || x >= image.axis.first) continue;

      int absCoords = x + y * image.axis.first;
      if(image.masked(x, y, Image::badPixel) || image.masked(x, y, Image::psf))
        return 0.0;

      if(image[absCoords] >= args.threshHigh) {
        image.maskPix(x, y, Image::badPixel);
        return 0.0;
      }
      if((image[absCoords] - stamp.stats.skyEst) / stamp.stats.fwhm >
         args.threshKernFit)
        retVal += image[absCoords];
    }
  }
  return retVal;
}

cl_int findSStamps(Stamp& stamp, Image& image, int index) {
  cl_double floor = stamp.stats.skyEst + args.threshKernFit * stamp.stats.fwhm;

  cl_double dfrac = 0.9;
  while(stamp.subStamps.size() < size_t(args.maxSStamps)) {
    long absx, absy, coords;
    cl_double lowestPSFLim =
        std::max(floor, stamp.stats.skyEst +
                            (args.threshHigh - stamp.stats.skyEst) * dfrac);
    for(long x = 0; x < stamp.size.first; x++) {
      absx = x + stamp.coords.first;
      for(long y = 0; y < stamp.size.second; y++) {
        absy = y + stamp.coords.second;
        coords = x + (y * stamp.size.first);

        if(image.masked(absx, absy, Image::badPixel) ||
           image.masked(absx, absy, Image::psf) ||
           image.masked(absx, absy, Image::edge)) {
          continue;
        }

        if(stamp[coords] > args.threshHigh) {
          image.maskPix(absx, absy, Image::badPixel);
          continue;
        }

        if((stamp[coords] - stamp.stats.skyEst) * (1.0 / stamp.stats.fwhm) <
           args.threshKernFit) {
          continue;
        }

        if(stamp[coords] > lowestPSFLim) {  // good candidate found
          SubStamp s{{},
                     0.0,
                     std::make_pair(absx, absy),
                     std::make_pair(x, y),
                     stamp[coords]};
          long kCoords;
          for(long kx = absx - args.hSStampWidth; kx < absx + args.hSStampWidth;
              kx++) {
            if(kx < 0 || kx >= image.axis.first) continue;
            for(long ky = absy - args.hSStampWidth;
                ky < absy + args.hSStampWidth; ky++) {
              if(ky < 0 || ky >= image.axis.second) continue;
              kCoords = kx + (ky * image.axis.first);

              if(image[kCoords] >= args.threshHigh) {
                image.maskPix(kx, ky, Image::badPixel);
                continue;
              }

              if(image.masked(kx, ky, Image::badPixel) ||
                 image.masked(kx, ky, Image::psf) ||
                 image.masked(kx, ky, Image::edge)) {
                continue;
              }

              if((image[kCoords] - stamp.stats.skyEst) *
                     (1.0 / stamp.stats.fwhm) <
                 args.threshKernFit) {
                continue;
              }

              if(image[kCoords] > s.val) {
                s.val = image[kCoords];
                s.imageCoords = std::make_pair(kx, ky);
                s.stampCoords = std::make_pair(kx - stamp.coords.first,
                                               ky - stamp.coords.second);
              }
            }
          }
          s.val = checkSStamp(s, image, stamp);
          if(s.val == 0) continue;
          stamp.subStamps.push_back(s);
          image.maskSStamp(s, Image::psf);
        }
        if(stamp.subStamps.size() >= size_t(args.maxSStamps)) break;
      }
      if(stamp.subStamps.size() >= size_t(args.maxSStamps)) break;
    }
    if(lowestPSFLim == floor) break;
    dfrac -= 0.2;
  }

  if(stamp.subStamps.size() == 0) {
    if(args.verbose)
      std::cout << "No suitable substamps found in stamp " << index
                << std::endl;
    return 1;
  }
  std::sort(stamp.subStamps.begin(), stamp.subStamps.end(),
            std::greater<SubStamp>());
  if(args.verbose)
    std::cout << "Added " << stamp.subStamps.size() << " substamps to stamp "
              << index << std::endl;
  return 0;
}