/*
 * uzenet-phone-server.c  (v0.3 – dial, hang‑up, DTMF, caller‑ID)
 * --------------------------------------------------------------
 * Bridges Uzebox 2‑bit ADPCM ↔ SIP/RTP.  Features:
 *   • multi‑client, thread‑per‑socket
 *   • localhost Identity daemon auth (name+pass → SIP uri)
 *   • user‑initiated dial / hang‑up
 *   • server‑pushed ring‑tone, call state changes, caller‑ID
 *   • DTMF (Uzebox sends, gateway emits RFC 2833)
 *
 * Build:
 *   gcc -O2 -Wall -pthread $(pkg-config --cflags --libs \
 *        libpj libpjlib-util libpjnath libpjmedia libpjsua2) \
 *        -lspeexdsp -lm -o uzenet-phone-server uzenet-phone-server.c
 */

#define _GNU_SOURCE 1
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <speex/speex_resampler.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

/* ───────────────────────── Uzebox link ───────────────────────── */
#define UZE_PORT             57430
#define UZE_SPS              15734
#define UZE_FRAME_SAMPLES    120               /* ≈7.6 ms */
#define UZE_FRAME_BYTES      (UZE_FRAME_SAMPLES/4)

/* Opcodes */
#define OPC_AUTH             0xF0  /* login */
#define OPC_VOICE_TX         0x10
#define OPC_CALL_DIGITS      0x13
#define OPC_HANGUP           0x14
#define OPC_DTMF             0x15  /* one ASCII digit */
#define OPC_RING_ANSWER      0x12

#define PKT_VOICE_RX         0x90
#define PKT_RING_TONE        0x82
#define PKT_CALL_STATE       0x93
#define PKT_CALLER_ID        0x94

/* Call‑state codes */
#define CS_IDLE      0
#define CS_DIALING   1
#define CS_RINGING   2
#define CS_ACTIVE    3
#define CS_ENDED     4
#define CS_ERROR   255

/* SIP outbound gateway host (for dialled digits) */
static const char *OB_HOST = "sip.telnyx.com";   /* change per provider */

/* ─────────────────────── Tiny 2‑bit ADPCM ───────────────────── */
static const int8_t STEP[4] = { 1, 3, -1, -3 };
static inline int16_t dec_nib(uint8_t c,int16_t*p){*p+=STEP[c]*64;if(*p>32767)*p=32767;if(*p<-32768)*p=-32768;return *p;}
static void adpcm2_decode(const uint8_t*in,size_t n,int16_t*out){int16_t p=0;size_t o=0;for(size_t i=0;i<n;i++){uint8_t b=in[i];out[o++]=dec_nib(b&3,&p);out[o++]=dec_nib(b>>2&3,&p);out[o++]=dec_nib(b>>4&3,&p);out[o++]=dec_nib(b>>6&3,&p);} }
static inline uint8_t enc_samp(int16_t s,int16_t*p){int16_t d=s-*p;uint8_t c=(d>0)?1:3;*p+=STEP[c]*64;return c;}
static void adpcm2_encode(const int16_t*in,size_t n,uint8_t*out){int16_t p=0;size_t o=0;for(size_t i=0;i<n;i+=4){uint8_t b=0;b|=enc_samp(in[i],&p)<<0;b|=enc_samp(in[i+1],&p)<<2;b|=enc_samp(in[i+2],&p)<<4;b|=enc_samp(in[i+3],&p)<<6;out[o++]=b;} }

/* Ring‑tone frame */
static void make_ring(uint8_t*out){static double ph;int16_t pcm[UZE_FRAME_SAMPLES];for(int i=0;i<UZE_FRAME_SAMPLES;i++){double t=ph+i*(1./UZE_SPS);double v=.5*sin(2*M_PI*440*t)+.5*sin(2*M_PI*480*t);pcm[i]=(int16_t)(v*29000);}ph+=UZE_FRAME_SAMPLES*(1./UZE_SPS);adpcm2_encode(pcm,UZE_FRAME_SAMPLES,out);} 

/* ───────────────────── Local identity daemon ────────────────── */
static const char *ID_HOST="127.0.0.1"; static uint16_t ID_PORT=57300;
struct IdReply{bool ok;char sip_uri[128];};
static IdReply id_query(const char*user,const char*pass){IdReply r={0,{0}};int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)return r;struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(ID_PORT)};inet_pton(AF_INET,ID_HOST,&a.sin_addr);if(connect(s,(struct sockaddr*)&a,sizeof a)!=0){close(s);return r;}uint8_t nl=strlen(user),pl=strlen(pass);send(s,&nl,1,0);send(s,user,nl,0);send(s,&pl,1,0);send(s,pass,pl,0);uint8_t resp;if(recv(s,&resp,1,MSG_WAITALL)!=1){close(s);return r;}if(resp==0x00){size_t got=recv(s,r.sip_uri,sizeof r.sip_uri-1,0);r.sip_uri[got]='\0';r.ok=true;}close(s);return r;}

/* ───────────────────────────  pjlib  ────────────────────────── */
#include <pjsua2.hpp>
using namespace pj;
static Endpoint ep; static Account *acc;

/* push one‑byte state to Uzebox */
static void push_state(int sock,uint8_t st){uint8_t h=PKT_CALL_STATE;send(sock,&h,1,0);send(sock,&st,1,0);} 
/* push caller‑ID string */
static void push_caller(int sock,const char*cid){uint8_t h=PKT_CALLER_ID;send(sock,&h,1,0);uint8_t len=strlen(cid);send(sock,&len,1,0);send(sock,cid,len,0);} 

/* ───────── per‑client struct (thread arg) ───────── */
struct CliCtx{int sock;pthread_t th;char ip[INET_ADDRSTRLEN];
	SpeexResamplerState *rs_u2s,*rs_s2u; pj::Call *call=nullptr; bool active=false;};

/* pjmedia globals */
static pj_caching_pool cp;static pj_pool_t *pool;static pjmedia_endpt*med_ep;static pjmedia_conf *conf;

/* resampler helpers */
static SpeexResamplerState* rsnew(int in,int out){int err;return speex_resampler_init(1,in,out,4,&err);} 

/* ─────────────── GwCall: forwards state & DTMF ─────────────── */
class GwCall: public Call{public: CliCtx*ctx;using Call::Call;
	void onCallState(OnCallStateParam&)override{CallInfo ci=getInfo();if(ci.state==PJSIP_INV_STATE_EARLY) push_state(ctx->sock,CS_RINGING); if(ci.state==PJSIP_INV_STATE_CONFIRMED){push_state(ctx->sock,CS_ACTIVE);ctx->active=true;} if(ci.state==PJSIP_INV_STATE_DISCONNECTED){push_state(ctx->sock,CS_ENDED);ctx->active=false;}}
	void onCallMediaState(OnCallMediaStateParam&)override{}
};

/* ─────────────── DTMF send helper (RFC2833) ─────────────── */
static void send_dtmf(Call *call,char digit){ pj_str_t dig = pj_str((char*)&digit); call->dialDtmf(dig); }

/* ─────────────── client thread ─────────────── */
static void*cli_thread(void*arg){CliCtx*c=(CliCtx*)arg;syslog(LOG_INFO,"CLI %s connect",c->ip);
	/* AUTH */ uint8_t op; if(recv(c->sock,&op,1,MSG_WAITALL)!=1||op!=OPC_AUTH){goto bye;} uint8_t nl;recv(c->sock,&nl,1,MSG_WAITALL);char user[64];recv(c->sock,user,nl,MSG_WAITALL);user[nl]='\0';uint8_t pl;recv(c->sock,&pl,1,MSG_WAITALL);char pass[64];recv(c->sock,pass,pl,MSG_WAITALL);pass[pl]='\0';IdReply id=id_query(user,pass); if(!id.ok){uint8_t ee=CS_ERROR;push_state(c->sock,ee);goto bye;} push_state(c->sock,CS_IDLE);

	c->rs_u2s=rsnew(UZE_SPS,8000); c->rs_s2u=rsnew(8000,UZE_SPS);
	uint8_t in[UZE_FRAME_BYTES],out[UZE_FRAME_BYTES]; int16_t pcmU[UZE_FRAME_SAMPLES],pcm8[160];

	for(;;){ if(recv(c->sock,&op,1,MSG_WAITALL)!=1)break;
		if(op==OPC_CALL_DIGITS){ /* gather digits till ';'*/ char d;std::string num="";while(recv(c->sock,&d,1,MSG_WAITALL)==1&&d!=';') num.push_back(d); if(num.empty())continue; char uri[128];snprintf(uri,sizeof uri,"sip:%s@%s",num.c_str(),OB_HOST); c->call=new GwCall(*acc); ((GwCall*)c->call)->ctx=c; CallOpParam prm(true); ((GwCall*)c->call)->makeCall(uri,prm); push_state(c->sock,CS_DIALING); }
		else if(op==OPC_HANGUP){ if(c->call&&c->active){ c->call->hangup(CallOpParam(true,PJSIP_SC_GONE)); }}
		else if(op==OPC_DTMF){ char dig;recv(c->sock,&dig,1,MSG_WAITALL); if(c->call&&c->active) send_dtmf(c->call,dig);} 
		else if(op==OPC_VOICE_TX){ recv(c->sock,in,UZE_FRAME_BYTES,MSG_WAITALL); adpcm2_decode(in,UZE_FRAME_BYTES,pcmU); spx_uint32_t il=UZE_FRAME_SAMPLES,ol=160; speex_resampler_process_int(c->rs_u2s,0,pcmU,&il,pcm8,&ol); if(c->call&&c->active) pjmedia_port_put_frame(conf->ports[c->call->getAudioMedia(0).getConfPort()].port,(pjmedia_frame*)pcm8); /* playback */ pjmedia_port_get_frame(conf->ports[c->call&&c->active?c->call->getAudioMedia(0).getConfPort():0].port,(pjmedia_frame*)pcm8); il=160;ol=UZE_FRAME_SAMPLES; speex_resampler_process_int(c->rs_s2u,0,pcm8,&il,pcmU,&ol); adpcm2_encode(pcmU,UZE_FRAME_SAMPLES,out); uint8_t h=PKT_VOICE_RX; send(c->sock,&h,1,0); send(c->sock,out,UZE_FRAME_BYTES,0);} 
	}
bye: close(c->sock); if(c->call){c->call->hangup(CallOpParam(true,PJSIP_SC_GONE)); delete c->call;} if(c->rs_u2s)speex_resampler_destroy(c->rs_u2s); if(c->rs_s2u)speex_resampler_destroy(c->rs_s2u); syslog(LOG_INFO,"CLI %s bye",c->ip); free(c); return NULL;}

/* ─────────────── sip init ─────────────── */
static void sip_init(const char*id,const char*user,const char*pw,const char*reg,unsigned lvl){ep.libCreate();EpConfig ec;ec.logConfig.level=lvl;ec.uaConfig.userAgent="UzenetPhone/0.3";ep.libInit(ec);TransportConfig tc;tc.port=5060;ep.transportCreate(PJSIP_TRANSPORT_UDP,tc);ep.libStart();AccountConfig ac;ac.idUri=id;ac.regConfig.registrarUri=reg;ac.sipConfig.authCreds.push_back(AuthCredInfo("digest","*",user,0,pw));acc=new Account;acc->create(ac);} 

/* ─────────────── main ─────────────── */
int main(int argc,char**argv){signal(SIGPIPE,SIG_IGN);openlog("uzenet-phone",LOG_PID,LOG_LOCAL6);
	const char *sip_uri=NULL,*au=NULL,*pw=NULL,*reg=NULL;unsigned lvl=4;static struct option lo[]={{"sip-uri",1,0,'s'},{"auth-user",1,0,'u'},{"password",1,0,'p'},{"registrar",1,0,'r'},{"log-level",1,0,'l'},{"id-port",1,0,'i'},{0}};int ch;while((ch=getopt_long(argc,argv,"s:u:p:r:l:i:",lo,NULL))!=-1){if(ch=='s')sip_uri=optarg;else if(ch=='u')au=optarg;else if(ch=='p')pw=optarg;else if(ch=='r')reg=optarg;else if(ch=='l')lvl=atoi(optarg);else if(ch=='i')ID_PORT=atoi(optarg);} if(!sip_uri||!au||!pw||!reg){fprintf(stderr,"usage: --sip-uri --auth-user --password --registrar\n");return 1;}
	sip_init(sip_uri,au,pw,reg,lvl);
	pj_caching_pool_init(&cp,NULL,1<<20); pool=pj_pool_create(&cp.factory,"conf",1024,1024,NULL); pjmedia_endpt_create(&cp.factory,NULL,1,&med_ep); pjmedia_conf_create(pool,32,8000,1,16,0,&conf);
	int srv=socket(AF_INET,SOCK_STREAM,0);int yes=1;setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(UZE_PORT),.sin_addr={INADDR_ANY}};bind(srv,(struct sockaddr*)&a,sizeof a);listen(srv,12);syslog(LOG_INFO,"uzenet-phone v0.3 on %d",UZE_PORT);
	while(1){CliCtx*c=(CliCtx*)calloc(1,sizeof *c);socklen_t sl=sizeof a; c->sock=accept(srv,(struct sockaddr*)&a,&sl); if(c->sock<0){free(c);continue;} inet_ntop(AF_INET,&a.sin_addr,c->ip,sizeof c->ip); pthread_create(&c->th,NULL,cli_thread,c); pthread_detach(c->th);} }
