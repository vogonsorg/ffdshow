#ifndef _TOUTPUTAUDIOSETTINGS_H_
#define _TOUTPUTAUDIOSETTINGS_H_

#include "TfilterSettings.h"

typedef std::hash_map<ffstring,ffstring> TdevicesList;

struct ToutputAudioSettings :TfilterSettingsAudio
{
private:
 static const TfilterIDFF idffs;
protected:
 const int* getResets(unsigned int pageId);
 TdevicesList getDevices(void);
public:
 ToutputAudioSettings(TintStrColl *Icoll=NULL,TfilterIDFFs *filters=NULL);
 int outsfs;
 int outAC3bitrate;
 int connectTo,connectToOnlySpdif;
 char_t multichannelDevice[255]; char_t multichannelDeviceId[255];
 static TdevicesList devicesList;
 static const char_t *connetTos[];
 virtual void createFilters(size_t filtersorder,Tfilters *filters,TfilterQueue &queue) const;
 virtual void createPages(TffdshowPageDec *parent) const {}
};

#endif
