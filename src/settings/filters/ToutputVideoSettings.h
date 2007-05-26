#ifndef _TOUTPUTVIDEOSETTINGS_H_
#define _TOUTPUTVIDEOSETTINGS_H_

#include "TfilterSettings.h"

#include "ffImgfmt.h"
struct ToutputVideoSettings :TfilterSettingsVideo
{
private:
 static const TfilterIDFF idffs;
protected:
 const int* getResets(unsigned int pageId);
 virtual int getDefault(int id);
public:
 ToutputVideoSettings(TintStrColl *Icoll=NULL,TfilterIDFFs *filters=NULL);

 int i420,yv12,yuy2,yvyu,uyvy,nv12,rgb32,rgb24,rgb555,rgb565;
 int closest;
 int dv,dvNorm;void getDVsize(unsigned int *dx,unsigned int *dy) const;
 static const char_t *dvNorms[];
 static const char_t *deintMethods[];

 struct ToutputColorspace
  {
   ToutputColorspace(DWORD IbiCompression,const GUID *Ig,WORD IbiBitCount,WORD IbiPlanes):biCompression(IbiCompression),g(Ig),biBitCount(IbiBitCount),biPlanes(IbiPlanes) {}
   DWORD biCompression;
   const GUID *g;
   WORD biBitCount,biPlanes;
  };
 void getOutputColorspaces(ints &ocsps),getOutputColorspaces(TcspInfos &ocsps);
 int hwOverlay,hwOverlayAspect;
 int hwOverlayOld,hwDeinterlaceOld;
 int allowOutChange3,outChangeCompatOnly;
 int hwDeinterlace,hwDeintMethod;
 int avisynthYV12_RGB;
 int flip;

 void reg_op_outcsps(TregOp &t);

 virtual void createFilters(size_t filtersorder,Tfilters *filters,TfilterQueue &queue) const {}
 virtual void createPages(TffdshowPageDec *parent) const {}
};

struct ToutputVideoConvertSettings :TfilterSettingsVideo
{
 ToutputVideoConvertSettings(TintStrColl *Icoll=NULL,TfilterIDFFs *filters=NULL):TfilterSettingsVideo(sizeof(*this),Icoll,filters,NULL,false) {}

 int csp;

 virtual void createFilters(size_t filtersorder,Tfilters *filters,TfilterQueue &queue) const {}
 virtual void createPages(TffdshowPageDec *parent) const {}
};

#endif
