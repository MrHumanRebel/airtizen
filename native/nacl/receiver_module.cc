/* WGT-embedded receiver: PPAPI owns lifecycle/audio; nacl_io supplies sockets. */
#include "airtizen_runtime.h"
#include "airtizen_control_http.h"
#include "airtizen_mdns.h"
#include "airtizen_capabilities.h"
#include "audio_queue.h"
#include "video_host.h"
#include <nacl_io/nacl_io.h>
#include <ppapi/cpp/audio.h>
#include <ppapi/cpp/video_decoder.h>
#include <ppapi/c/ppb_video_decoder.h>
#include <ppapi/c/pp_graphics_3d.h>
#include <ppapi/cpp/audio_config.h>
#include <ppapi/cpp/core.h>
#include <ppapi/cpp/instance.h>
#include <ppapi/cpp/module.h>
#include <ppapi/cpp/var.h>
#include <ppapi/utility/completion_callback_factory.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <string>

namespace {
class AudioHost {
 public:
  explicit AudioHost(pp::Instance *instance):instance_(instance),callbacks_(this),
      rate_(0),channels_(0),desired_(0),result_(-1),opened_(false),closing_(false),pending_(false) {
    pthread_mutex_init(&mutex_,NULL);pthread_cond_init(&condition_,NULL);airtizen_audio_queue_init(&queue_);
  }
  ~AudioHost(){Shutdown();airtizen_audio_queue_destroy(&queue_);pthread_cond_destroy(&condition_);pthread_mutex_destroy(&mutex_);}
  int Open(int rate,int channels,int bits) {
    if((rate!=44100 && rate!=48000) || (channels!=1 && channels!=2) || bits!=16)return -1;
    pthread_mutex_lock(&mutex_);
    if(closing_){pthread_mutex_unlock(&mutex_);return -1;}
    if(opened_ && rate==rate_ && channels==channels_){pthread_mutex_unlock(&mutex_);return 0;}
    if(rate!=rate_ || audio_.is_null()) {
      desired_=rate;pending_=true;
      pp::Module::Get()->core()->CallOnMainThread(0,callbacks_.NewCallback(&AudioHost::Configure));
      while(pending_ && !closing_)pthread_cond_wait(&condition_,&mutex_);
      if(closing_ || result_){pthread_mutex_unlock(&mutex_);return -1;}
    }
    channels_=channels;opened_=true;airtizen_audio_queue_open(&queue_,channels);
    pthread_mutex_unlock(&mutex_);return 0;
  }
  int Write(const void *pcm,size_t bytes){return airtizen_audio_queue_write(&queue_,pcm,bytes);}
  void Close(){pthread_mutex_lock(&mutex_);opened_=false;airtizen_audio_queue_close(&queue_);pthread_mutex_unlock(&mutex_);}
  void Shutdown(){
    pthread_mutex_lock(&mutex_);closing_=true;pending_=false;pthread_cond_broadcast(&condition_);pthread_mutex_unlock(&mutex_);
    callbacks_.CancelAll();
    if(!audio_.is_null()){audio_.StopPlayback();audio_=pp::Audio();}
    airtizen_audio_queue_close(&queue_);
  }
 private:
  static void Render(void *samples,uint32_t bytes,PP_TimeDelta,void *opaque) {
    airtizen_audio_queue_render(&static_cast<AudioHost*>(opaque)->queue_,samples,bytes);
  }
  void Configure(int32_t) {
    pthread_mutex_lock(&mutex_);
    if(closing_){pthread_mutex_unlock(&mutex_);return;}
    int rate=desired_;pthread_mutex_unlock(&mutex_);
    if(!audio_.is_null())audio_.StopPlayback();
    PP_AudioSampleRate sample_rate=static_cast<PP_AudioSampleRate>(rate);
    uint32_t frames=pp::AudioConfig::RecommendSampleFrameCount(instance_,sample_rate,512);
    pp::AudioConfig config(instance_,sample_rate,frames);
    audio_=pp::Audio(instance_,config,&AudioHost::Render,this);
    int result=audio_.is_null() || !audio_.StartPlayback() ? -1:0;
    pthread_mutex_lock(&mutex_);result_=result;if(!result)rate_=rate;pending_=false;
    pthread_cond_broadcast(&condition_);pthread_mutex_unlock(&mutex_);
  }
  pp::Instance *instance_;pp::CompletionCallbackFactory<AudioHost> callbacks_;pp::Audio audio_;
  airtizen_audio_queue queue_;pthread_mutex_t mutex_;pthread_cond_t condition_;
  int rate_,channels_,desired_,result_;bool opened_,closing_,pending_;
};
AudioHost *audio_host;
VideoHost *video_host;

class ReceiverInstance:public pp::Instance {
 public:
  explicit ReceiverInstance(PP_Instance instance):pp::Instance(instance),audio_(this),started_(false),video_callbacks_(this),video_(this){}
  virtual bool Init(uint32_t argc,const char *argn[],const char *argv[]) {
    if(audio_host)return false; /* The core deliberately owns one receiver. */
    std::string address,id,token;name_="AirTizen TV";
    for(uint32_t i=0;i<argc;++i) {
      if(!strcmp(argn[i],"receiver-ip"))address=argv[i];
      else if(!strcmp(argn[i],"receiver-token"))token=argv[i];
      else if(!strcmp(argn[i],"receiver-id"))id=argv[i];
      else if(!strcmp(argn[i],"receiver-name"))name_=argv[i];
    }
    if(airtizen_control_http_set_token(token.c_str()))return false;
    if(airtizen_mdns_set_network(address.c_str(),id.c_str()))return false;
    if(airtizen_discovery_set_profile("native"))return false;
    if(nacl_io_init_ppapi(pp_instance(),pp::Module::Get()->get_browser_interface()))return false;
    audio_host=&audio_;video_host=&video_;
    if(pthread_create(&worker_,NULL,&ReceiverInstance::Start,this)){audio_host=NULL;return false;}
    started_=true;return true;
  }
  virtual void HandleMessage(const pp::Var& message) {
    if(!message.is_string())return;
    if(message.AsString()=="airtizen-video-on"){video_.Enable(true);return;}
    if(message.AsString()=="airtizen-video-off"){video_.Enable(false);return;}
    if(message.AsString()!="airtizen-probe-video")return;
    if(!pp::Module::Get()->GetBrowserInterface(PPB_VIDEODECODER_INTERFACE)) {
      PostMessage(pp::Var("airtizen-video-probe:interface-unavailable"));return;
    }
    int32_t attributes[]={PP_GRAPHICS3DATTRIB_WIDTH,16,PP_GRAPHICS3DATTRIB_HEIGHT,16,PP_GRAPHICS3DATTRIB_NONE};
    probe_graphics_=pp::Graphics3D(this,attributes);
    probe_decoder_=pp::VideoDecoder(this);
    if(probe_graphics_.is_null() || probe_decoder_.is_null()) {
      PostMessage(pp::Var("airtizen-video-probe:resource-unavailable"));return;
    }
    probe_decoder_.Initialize(probe_graphics_,PP_VIDEOPROFILE_H264HIGH,PP_HARDWAREACCELERATION_ONLY,0,
      video_callbacks_.NewCallback(&ReceiverInstance::ProbeVideoDone));
  }
  virtual ~ReceiverInstance(){
    video_callbacks_.CancelAll();probe_decoder_=pp::VideoDecoder();probe_graphics_=pp::Graphics3D();
    video_.Shutdown();if(video_host==&video_)video_host=NULL;
    audio_.Shutdown();
    if(started_){pthread_join(worker_,NULL);airtizen_runtime_stop();airtizen_control_http_stop();}
    if(audio_host==&audio_)audio_host=NULL;
  }
 private:
  void ProbeVideoDone(int32_t result) {
    char text[80];snprintf(text,sizeof(text),"airtizen-video-probe:initialize=%d",(int)result);
    PostMessage(pp::Var(text));probe_decoder_=pp::VideoDecoder();probe_graphics_=pp::Graphics3D();
  }
  static void *Start(void *opaque){
    ReceiverInstance *self=static_cast<ReceiverInstance*>(opaque);
    if(airtizen_control_http_start(45110)==0)airtizen_runtime_start(self->name_.c_str());
    return NULL;
  }
  AudioHost audio_;pthread_t worker_;bool started_;std::string name_;
  pp::CompletionCallbackFactory<ReceiverInstance> video_callbacks_;pp::Graphics3D probe_graphics_;pp::VideoDecoder probe_decoder_;VideoHost video_;
};
class ReceiverModule:public pp::Module {
 public: virtual pp::Instance *CreateInstance(PP_Instance instance){return new ReceiverInstance(instance);}
};
}
extern "C" void airtizen_nacl_video_configure(const uint8_t *data,size_t size){if(video_host)video_host->Configure(data,size);}
extern "C" void airtizen_nacl_video_frame(const uint8_t *data,size_t size){if(video_host)video_host->Frame(data,size);}
extern "C" void airtizen_nacl_video_reset(void){if(video_host)video_host->Reset();}
extern "C" uint64_t airtizen_nacl_now_us(void){return static_cast<uint64_t>(pp::Module::Get()->core()->GetTimeTicks()*1000000.0);}
extern "C" int airtizen_audio_open(int rate,int channels,int bits){return audio_host?audio_host->Open(rate,channels,bits):-1;}
extern "C" int airtizen_audio_write(const void *pcm,size_t bytes){return audio_host?audio_host->Write(pcm,bytes):-1;}
extern "C" void airtizen_audio_close(void){if(audio_host)audio_host->Close();}
namespace pp {Module *CreateModule(){return new ReceiverModule();}}
