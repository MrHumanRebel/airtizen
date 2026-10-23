/* Direct PPAPI hardware H.264 presentation for legacy Tizen.
 * GL presentation follows Chromium Pepper47 video_decode's BSD example.
 * Copyright (c) 2014 The Chromium Authors. All rights reserved.
 * The original example is distributed under the Chromium BSD license.
 */
#ifndef AIRTIZEN_VIDEO_HOST_H
#define AIRTIZEN_VIDEO_HOST_H
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <ppapi/c/ppb_opengles2.h>
#include <ppapi/c/pp_errors.h>
#include <ppapi/cpp/video_decoder.h>
#include <ppapi/cpp/core.h>
#include <ppapi/cpp/graphics_3d.h>
#include <ppapi/cpp/instance.h>
#include <ppapi/cpp/module.h>
#include <ppapi/cpp/var.h>
#include <ppapi/utility/completion_callback_factory.h>
#include <pthread.h>
#include <deque>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <new>
#include <algorithm>
class VideoHost {
 struct Timing {double received,feed,picture,paint,swap;};
 struct Packet {std::vector<uint8_t> bytes;double received;};
 public:
 explicit VideoHost(pp::Instance *instance):instance_(instance),dispatch_(this),callbacks_(this),enabled_(false),closing_(false),posted_(false),configure_(false),reset_(false),failed_(false),ready_(false),decoding_(false),painting_(false),pending_(false),queued_bytes_(0),gl_(NULL),program_(0),target_(0),buffer_(0),next_id_(0),frames_(0),dropped_(0),last_ms_(0),sum_ms_(0),optimized_(false),drain_(false),aud_(false),flushing_(false),decoding_config_(false),flushes_(0),scale_location_(-1),offset_location_(-1),window_count_(0),window_next_(0),queue_sum_(0),decode_sum_(0),wait_sum_(0),draw_sum_(0),swap_sum_(0) {
  pthread_mutex_init(&mutex_,NULL);memset(received_,0,sizeof(received_));memset(timing_,0,sizeof(timing_));memset(window_,0,sizeof(window_));
 }
 ~VideoHost(){Shutdown();pthread_mutex_destroy(&mutex_);}
 void Delimit(bool enabled){aud_=enabled;Optimize(optimized_);}
 void Drain(bool enabled){drain_=enabled;flushes_=0;Optimize(optimized_);}
 void Optimize(bool enabled) {
  optimized_=enabled;frames_=dropped_=0;sum_ms_=last_ms_=0;window_count_=window_next_=0;
  queue_sum_=decode_sum_=wait_sum_=draw_sum_=swap_sum_=0;Message("configured");
 }
 void Enable(bool enabled) {
  pthread_mutex_lock(&mutex_);enabled_=enabled;reset_=true;configure_=false;failed_=false;queue_.clear();queued_bytes_=0;config_.clear();PostLocked();pthread_mutex_unlock(&mutex_);
 }
 void Configure(const uint8_t *data,size_t size) {
  if(!data || !size || size>65536)return;
  pthread_mutex_lock(&mutex_);
  if(enabled_&&!closing_) {
   try {config_.assign(data,data+size);queue_.clear();queued_bytes_=0;configure_=true;reset_=false;failed_=false;PostLocked();}catch(const std::bad_alloc&){failed_=true;PostLocked();}
  }
  pthread_mutex_unlock(&mutex_);
 }
 void Frame(const uint8_t *data,size_t size) {
  if(!data || !size || size>2U*1024U*1024U)return;
  pthread_mutex_lock(&mutex_);
  if(enabled_&&!closing_&&!failed_&&!reset_) {
   if(queue_.size()>=12 || queued_bytes_+size>8U*1024U*1024U){failed_=true;PostLocked();}
   else try {Packet p;p.bytes.assign(data,data+size);p.received=pp::Module::Get()->core()->GetTimeTicks();queued_bytes_+=size;queue_.push_back(p);PostLocked();}catch(const std::bad_alloc&){failed_=true;PostLocked();}
  }
  pthread_mutex_unlock(&mutex_);
 }
 void Reset() {pthread_mutex_lock(&mutex_);reset_=true;configure_=false;queue_.clear();queued_bytes_=0;config_.clear();PostLocked();pthread_mutex_unlock(&mutex_);}
 void Shutdown() {
  pthread_mutex_lock(&mutex_);closing_=true;enabled_=false;queue_.clear();queued_bytes_=0;pthread_mutex_unlock(&mutex_);
  dispatch_.CancelAll();CloseDecoder();
 }
 private:
 void PostLocked(){if(!posted_){posted_=true;pp::Module::Get()->core()->CallOnMainThread(0,dispatch_.NewCallback(&VideoHost::Pump));}}
 void Message(const char *state,int code=0) {
  char text[1024];size_t queued;pthread_mutex_lock(&mutex_);queued=queue_.size();pthread_mutex_unlock(&mutex_);
  double ordered[128],recent=0;for(unsigned i=0;i<window_count_;++i){ordered[i]=window_[i];recent+=window_[i];}
  std::sort(ordered,ordered+window_count_);
  double n=frames_?frames_:1;
  snprintf(text,sizeof(text),"{\"type\":\"airtizen-native-video\",\"state\":\"%s\",\"code\":%d,\"frames\":%u,\"droppedPictures\":%u,\"queuedFrames\":%u,\"lastMs\":%.3f,\"averageMs\":%.3f,\"optimized\":%s,\"drain\":%s,\"aud\":%s,\"flushes\":%u,\"recentAverageMs\":%.3f,\"p50Ms\":%.3f,\"p95Ms\":%.3f,\"queueMs\":%.3f,\"decodeMs\":%.3f,\"renderWaitMs\":%.3f,\"drawMs\":%.3f,\"swapMs\":%.3f}",state,code,frames_,dropped_,(unsigned)queued,last_ms_,sum_ms_/n,optimized_?"true":"false",drain_?"true":"false",aud_?"true":"false",flushes_,window_count_?recent/window_count_:0,window_count_?ordered[window_count_/2]:0,window_count_?ordered[(window_count_-1)*95/100]:0,queue_sum_/n,decode_sum_/n,wait_sum_/n,draw_sum_/n,swap_sum_/n);
  instance_->PostMessage(pp::Var(text));
 }
 void Error(int code) {pthread_mutex_lock(&mutex_);failed_=true;queue_.clear();queued_bytes_=0;pthread_mutex_unlock(&mutex_);ready_=false;Message("error",code);}
 void CloseDecoder() {
  callbacks_.CancelAll();ready_=false;decoding_=false;flushing_=false;painting_=false;pending_=false;
  decoder_=pp::VideoDecoder();instance_->BindGraphics(pp::Graphics3D());graphics_=pp::Graphics3D();program_=target_=buffer_=0;
 }
 void Pump(int32_t=0) {
  std::vector<uint8_t> config;bool reset=false,configure=false,failed=false,enabled=false;
  pthread_mutex_lock(&mutex_);posted_=false;
  if(closing_){pthread_mutex_unlock(&mutex_);return;}
  enabled=enabled_;reset=reset_;reset_=false;configure=configure_;configure_=false;failed=failed_;
  if(configure)config=config_;
  pthread_mutex_unlock(&mutex_);
  if(reset||!enabled){CloseDecoder();Message("idle");return;}
  if(failed){Error(PP_ERROR_NOMEMORY);return;}
  if(configure){
   CloseDecoder();frames_=dropped_=0;sum_ms_=last_ms_=0;window_count_=window_next_=0;queue_sum_=decode_sum_=wait_sum_=draw_sum_=swap_sum_=0;
   gl_=static_cast<const PPB_OpenGLES2*>(pp::Module::Get()->GetBrowserInterface(PPB_OPENGLES2_INTERFACE));
   if(!gl_){Error(PP_ERROR_NOINTERFACE);return;}
   int32_t attributes[]={PP_GRAPHICS3DATTRIB_ALPHA_SIZE,0,PP_GRAPHICS3DATTRIB_DEPTH_SIZE,0,PP_GRAPHICS3DATTRIB_STENCIL_SIZE,0,PP_GRAPHICS3DATTRIB_WIDTH,1920,PP_GRAPHICS3DATTRIB_HEIGHT,1080,PP_GRAPHICS3DATTRIB_NONE};
   graphics_=pp::Graphics3D(instance_,attributes);
   if(graphics_.is_null()||!instance_->BindGraphics(graphics_)){Error(PP_ERROR_FAILED);return;}
   static const float vertices[]={-1,-1,-1,1,1,-1,1,1, 0,1,0,0,1,1,1,0};
   PP_Resource c=graphics_.pp_resource();gl_->GenBuffers(c,1,&buffer_);gl_->BindBuffer(c,GL_ARRAY_BUFFER,buffer_);gl_->BufferData(c,GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
   decoder_=pp::VideoDecoder(instance_);if(decoder_.is_null()){Error(PP_ERROR_NOINTERFACE);return;}
   decoder_.Initialize(graphics_,PP_VIDEOPROFILE_H264HIGH,PP_HARDWAREACCELERATION_ONLY,0,callbacks_.NewCallback(&VideoHost::Initialized));
   Message("initializing");return;
  }
  if(!ready_||decoding_||flushing_)return;
  Packet packet;decoding_config_=false;
  pthread_mutex_lock(&mutex_);
  if(!config_.empty()){decoding_config_=true;packet.bytes.swap(config_);packet.received=pp::Module::Get()->core()->GetTimeTicks();}
  else if(!queue_.empty()){packet=queue_.front();queued_bytes_-=packet.bytes.size();queue_.pop_front();}
  pthread_mutex_unlock(&mutex_);
  if(packet.bytes.empty())return;
  if(aud_&&!decoding_config_){static const uint8_t delimiter[]={0,0,0,1,9,0xf0};packet.bytes.insert(packet.bytes.end(),delimiter,delimiter+sizeof(delimiter));}
  decoding_=true;uint32_t id=++next_id_;received_[id%128]=packet.received;Timing &timing=timing_[id%128];memset(&timing,0,sizeof(timing));timing.received=packet.received;timing.feed=pp::Module::Get()->core()->GetTimeTicks();
  decoder_.Decode(id,(uint32_t)packet.bytes.size(),&packet.bytes[0],callbacks_.NewCallback(&VideoHost::Decoded));
 }
 void Initialized(int32_t result) {
  if(result!=PP_OK){Error(result);return;}ready_=true;
  decoder_.GetPicture(callbacks_.NewCallbackWithOutput(&VideoHost::Picture));Pump();
 }
 void Decoded(int32_t result) {
  decoding_=false;if(result==PP_ERROR_ABORTED)return;if(result!=PP_OK){Error(result);return;}
  if(drain_&&!decoding_config_){flushing_=true;decoder_.Flush(callbacks_.NewCallback(&VideoHost::Flushed));return;}
  Pump();
 }
 void Flushed(int32_t result) {
  flushing_=false;if(result==PP_ERROR_ABORTED)return;if(result!=PP_OK){Error(result);return;}++flushes_;
  decoder_.GetPicture(callbacks_.NewCallbackWithOutput(&VideoHost::Picture));Pump();
 }
 void Picture(int32_t result,PP_VideoPicture picture) {
  if(result==PP_ERROR_ABORTED)return;
  if(result!=PP_OK){Error(result);return;}
  timing_[picture.decode_id%128].picture=pp::Module::Get()->core()->GetTimeTicks();
  if(painting_){if(pending_){decoder_.RecyclePicture(next_picture_);++dropped_;}next_picture_=picture;pending_=true;}
  else Paint(picture);
  if(ready_)decoder_.GetPicture(callbacks_.NewCallbackWithOutput(&VideoHost::Picture));
 }
 bool Program(GLenum target) {
  if(program_&&target_==target)return true;
  PP_Resource c=graphics_.pp_resource();if(program_)gl_->DeleteProgram(c,program_);
  const char *vertex="attribute vec2 p;attribute vec2 t;uniform vec2 scale;uniform vec2 offset;varying vec2 uv;void main(){gl_Position=vec4(p,0.,1.);uv=offset+t*scale;}";
  const char *fragment2d="precision mediump float;varying vec2 uv;uniform sampler2D tex;void main(){gl_FragColor=texture2D(tex,uv);}";
  const char *fragmentExternal="#extension GL_OES_EGL_image_external : require\nprecision mediump float;varying vec2 uv;uniform samplerExternalOES tex;void main(){gl_FragColor=texture2D(tex,uv);}";
  const char *fragmentRect="#extension GL_ARB_texture_rectangle : require\nprecision mediump float;varying vec2 uv;uniform sampler2DRect tex;void main(){gl_FragColor=texture2DRect(tex,uv);}";
  const char *sources[]={vertex,target==GL_TEXTURE_EXTERNAL_OES?fragmentExternal:target==GL_TEXTURE_RECTANGLE_ARB?fragmentRect:fragment2d};
  program_=gl_->CreateProgram(c);
  for(int i=0;i<2;++i){GLuint shader=gl_->CreateShader(c,i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);gl_->ShaderSource(c,shader,1,sources+i,NULL);gl_->CompileShader(c,shader);GLint ok=0;gl_->GetShaderiv(c,shader,GL_COMPILE_STATUS,&ok);if(!ok){gl_->DeleteShader(c,shader);Error(PP_ERROR_FAILED);return false;}gl_->AttachShader(c,program_,shader);gl_->DeleteShader(c,shader);}
  gl_->LinkProgram(c,program_);GLint linked=0;gl_->GetProgramiv(c,program_,GL_LINK_STATUS,&linked);if(!linked){Error(PP_ERROR_FAILED);return false;}
  gl_->UseProgram(c,program_);gl_->BindBuffer(c,GL_ARRAY_BUFFER,buffer_);
  GLint p=gl_->GetAttribLocation(c,program_,"p"),t=gl_->GetAttribLocation(c,program_,"t");
  gl_->EnableVertexAttribArray(c,p);gl_->VertexAttribPointer(c,p,2,GL_FLOAT,GL_FALSE,0,NULL);
  gl_->EnableVertexAttribArray(c,t);gl_->VertexAttribPointer(c,t,2,GL_FLOAT,GL_FALSE,0,reinterpret_cast<void*>(8*sizeof(float)));
  gl_->Uniform1i(c,gl_->GetUniformLocation(c,program_,"tex"),0);scale_location_=gl_->GetUniformLocation(c,program_,"scale");offset_location_=gl_->GetUniformLocation(c,program_,"offset");target_=target;return true;
 }
 void Paint(const PP_VideoPicture& picture) {
  timing_[picture.decode_id%128].paint=pp::Module::Get()->core()->GetTimeTicks();
  if(!Program(picture.texture_target)){decoder_.RecyclePicture(picture);return;}
  int width=picture.visible_rect.size.width,height=picture.visible_rect.size.height;
  if(width<=0||height<=0||picture.texture_size.width<=0||picture.texture_size.height<=0){decoder_.RecyclePicture(picture);Error(PP_ERROR_BADARGUMENT);return;}
  PP_Resource c=graphics_.pp_resource();gl_->UseProgram(c,program_);
  float sx=(float)width/picture.texture_size.width,sy=(float)height/picture.texture_size.height;
  float ox=(float)picture.visible_rect.point.x/picture.texture_size.width,oy=(float)picture.visible_rect.point.y/picture.texture_size.height;
  if(picture.texture_target==GL_TEXTURE_RECTANGLE_ARB){sx=width;sy=height;ox=picture.visible_rect.point.x;oy=picture.visible_rect.point.y;}
  gl_->Uniform2f(c,(optimized_?scale_location_:gl_->GetUniformLocation(c,program_,"scale")),sx,sy);gl_->Uniform2f(c,(optimized_?offset_location_:gl_->GetUniformLocation(c,program_,"offset")),ox,oy);
  gl_->Viewport(c,0,0,1920,1080);gl_->ClearColor(c,0,0,0,1);gl_->Clear(c,GL_COLOR_BUFFER_BIT);
  int w=1920,h=1080;if((double)width/height<1920.0/1080)w=(int)(1080.0*width/height);else h=(int)(1920.0*height/width);
  gl_->Viewport(c,(1920-w)/2,(1080-h)/2,w,h);gl_->ActiveTexture(c,GL_TEXTURE0);gl_->BindTexture(c,picture.texture_target,picture.texture_id);gl_->DrawArrays(c,GL_TRIANGLE_STRIP,0,4);
  if((!optimized_ || frames_%120==0) && gl_->GetError(c)!=GL_NO_ERROR){decoder_.RecyclePicture(picture);Error(PP_ERROR_FAILED);return;}
  timing_[picture.decode_id%128].swap=pp::Module::Get()->core()->GetTimeTicks();
  painting_=true;current_picture_=picture;graphics_.SwapBuffers(callbacks_.NewCallback(&VideoHost::Painted));
 }
 void Painted(int32_t result) {
  painting_=false;if(result==PP_ERROR_ABORTED)return;if(result!=PP_OK){Error(result);return;}
  double finished=pp::Module::Get()->core()->GetTimeTicks();Timing &t=timing_[current_picture_.decode_id%128];
  queue_sum_+=1000*(t.feed-t.received);decode_sum_+=1000*(t.picture-t.feed);wait_sum_+=1000*(t.paint-t.picture);draw_sum_+=1000*(t.swap-t.paint);swap_sum_+=1000*(finished-t.swap);
  last_ms_=1000*(pp::Module::Get()->core()->GetTimeTicks()-received_[current_picture_.decode_id%128]);sum_ms_+=last_ms_;++frames_;window_[window_next_]=last_ms_;window_next_=(window_next_+1)%128;if(window_count_<128)++window_count_;
  decoder_.RecyclePicture(current_picture_);if(frames_==1||frames_%30==0)Message("playing");
  if(pending_){PP_VideoPicture next=next_picture_;pending_=false;Paint(next);}
 }
 pp::Instance *instance_;pp::CompletionCallbackFactory<VideoHost> dispatch_,callbacks_;
 pthread_mutex_t mutex_;bool enabled_,closing_,posted_,configure_,reset_,failed_,ready_,decoding_,painting_,pending_;size_t queued_bytes_;
 std::deque<Packet> queue_;std::vector<uint8_t> config_;pp::Graphics3D graphics_;pp::VideoDecoder decoder_;
 const PPB_OpenGLES2 *gl_;GLuint program_,target_,buffer_;uint32_t next_id_,frames_,dropped_;double received_[128],last_ms_,sum_ms_;
 bool optimized_,drain_,aud_,flushing_,decoding_config_;unsigned flushes_;GLint scale_location_,offset_location_;unsigned window_count_,window_next_;double window_[128],queue_sum_,decode_sum_,wait_sum_,draw_sum_,swap_sum_;Timing timing_[128];
 PP_VideoPicture current_picture_,next_picture_;
};
#endif
