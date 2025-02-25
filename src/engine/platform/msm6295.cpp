/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2022 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "msm6295.h"
#include "../engine.h"
#include "../../ta-log.h"
#include <string.h>
#include <math.h>

#define rWrite(a,v) if (!skipRegisterWrites) {writes.emplace(a,v); if (dumpWrites) {addWrite(a,v);} }

const char** DivPlatformMSM6295::getRegisterSheet() {
  return NULL;
}

const char* DivPlatformMSM6295::getEffectName(unsigned char effect) {
  switch (effect) {
    case 0x20:
      return "20xx: Set chip output rate (0: clock/132; 1: clock/165)";
      break; 
  }
  return NULL;
}

u8 DivMSM6295Interface::read_byte(u32 address) {
  return adpcmMem[address&0xffff];
}

void DivPlatformMSM6295::acquire(short* bufL, short* bufR, size_t start, size_t len) {
  for (size_t h=start; h<start+len; h++) {
    if (delay<=0) {
      if (!writes.empty()) {
        QueuedWrite& w=writes.front();
        switch (w.addr) {
          case 0: // command
            msm->command_w(w.val);
            break;
          case 8: // chip clock select (VGM)
          case 9:
          case 10:
          case 11:
            break;
          case 12: // rate select
            msm->ss_w(!w.val);
            break;
          case 14: // enable bankswitch
            break;
          case 15: // set bank base
            break;
          case 16: // switch bank
          case 17:
          case 18:
          case 19:
            break;
        }
        writes.pop();
        delay=32;
      }
    } else {
      delay--;
    }
    
    msm->tick();
  
    bufL[h]=msm->out()<<4;

    if (++updateOsc>=22) {
      updateOsc=0;
      // TODO: per-channel osc
      for (int i=0; i<4; i++) {
        oscBuf[i]->data[oscBuf[i]->needle++]=msm->m_voice[i].m_muted?0:(msm->m_voice[i].m_out<<6);
      }
    }
  }
}

void DivPlatformMSM6295::tick(bool sysTick) {
  // nothing
}

int DivPlatformMSM6295::dispatch(DivCommand c) {
  switch (c.cmd) {
    case DIV_CMD_NOTE_ON: {
      DivInstrument* ins=parent->getIns(chan[c.chan].ins,DIV_INS_FM);
      if (ins->type==DIV_INS_AMIGA) {
        chan[c.chan].furnacePCM=true;
      } else {
        chan[c.chan].furnacePCM=false;
      }
      if (skipRegisterWrites) break;
      if (chan[c.chan].furnacePCM) {
        chan[c.chan].macroInit(ins);
        if (!chan[c.chan].std.vol.will) {
          chan[c.chan].outVol=chan[c.chan].vol;
        }
        chan[c.chan].sample=ins->amiga.getSample(c.value);
        if (chan[c.chan].sample>=0 && chan[c.chan].sample<parent->song.sampleLen) {
          //DivSample* s=parent->getSample(chan[c.chan].sample);
          if (c.value!=DIV_NOTE_NULL) {
            chan[c.chan].note=c.value;
            chan[c.chan].freqChanged=true;
          }
          chan[c.chan].active=true;
          chan[c.chan].keyOn=true;
          rWrite(0,(8<<c.chan)); // turn off
          rWrite(0,0x80|chan[c.chan].sample); // set phrase
          rWrite(0,(16<<c.chan)|(8-chan[c.chan].outVol)); // turn on
        } else {
          break;
        }
      } else {
        chan[c.chan].sample=-1;
        chan[c.chan].macroInit(NULL);
        chan[c.chan].outVol=chan[c.chan].vol;
        if ((12*sampleBank+c.value%12)>=parent->song.sampleLen) {
          break;
        }
        //DivSample* s=parent->getSample(12*sampleBank+c.value%12);
        chan[c.chan].sample=12*sampleBank+c.value%12;
        rWrite(0,(8<<c.chan)); // turn off
        rWrite(0,0x80|chan[c.chan].sample); // set phrase
        rWrite(0,(16<<c.chan)|(8-chan[c.chan].outVol)); // turn on
      }
      break;
    }
    case DIV_CMD_NOTE_OFF:
      chan[c.chan].keyOff=true;
      chan[c.chan].keyOn=false;
      chan[c.chan].active=false;
      rWrite(0,(8<<c.chan)); // turn off
      chan[c.chan].macroInit(NULL);
      break;
    case DIV_CMD_NOTE_OFF_ENV:
      chan[c.chan].keyOff=true;
      chan[c.chan].keyOn=false;
      chan[c.chan].active=false;
      rWrite(0,(8<<c.chan)); // turn off
      chan[c.chan].std.release();
      break;
    case DIV_CMD_ENV_RELEASE:
      chan[c.chan].std.release();
      break;
    case DIV_CMD_VOLUME: {
      chan[c.chan].vol=c.value;
      if (!chan[c.chan].std.vol.has) {
        chan[c.chan].outVol=c.value;
      }
      break;
    }
    case DIV_CMD_GET_VOLUME: {
      return chan[c.chan].vol;
      break;
    }
    case DIV_CMD_INSTRUMENT:
      if (chan[c.chan].ins!=c.value || c.value2==1) {
        chan[c.chan].insChanged=true;
      }
      chan[c.chan].ins=c.value;
      break;
    case DIV_CMD_PITCH: {
      break;
    }
    case DIV_CMD_NOTE_PORTA: {
      return 2;
    }
    case DIV_CMD_SAMPLE_FREQ:
      rateSel=c.value;
      rWrite(12,!rateSel);
      break;
    case DIV_CMD_SAMPLE_BANK:
      sampleBank=c.value;
      if (sampleBank>(parent->song.sample.size()/12)) {
        sampleBank=parent->song.sample.size()/12;
      }
      iface.sampleBank=sampleBank;
      break;
    case DIV_CMD_LEGATO: {
      break;
    }
    case DIV_ALWAYS_SET_VOLUME:
      return 0;
      break;
    case DIV_CMD_GET_VOLMAX:
      return 8;
      break;
    case DIV_CMD_PRE_PORTA:
      break;
    case DIV_CMD_PRE_NOTE:
      break;
    default:
      //printf("WARNING: unimplemented command %d\n",c.cmd);
      break;
  }
  return 1;
}

void DivPlatformMSM6295::muteChannel(int ch, bool mute) {
  isMuted[ch]=mute;
  msm->m_voice[ch].m_muted=mute;
}

void DivPlatformMSM6295::forceIns() {
  while (!writes.empty()) writes.pop();
  for (int i=0; i<4; i++) {
    chan[i].insChanged=true;
  }
  rWrite(12,!rateSel);
}

void* DivPlatformMSM6295::getChanState(int ch) {
  return &chan[ch];
}

DivDispatchOscBuffer* DivPlatformMSM6295::getOscBuffer(int ch) {
  return oscBuf[ch];
}

unsigned char* DivPlatformMSM6295::getRegisterPool() {
  return NULL;
}

int DivPlatformMSM6295::getRegisterPoolSize() {
  return 0;
}

void DivPlatformMSM6295::poke(unsigned int addr, unsigned short val) {
  //immWrite(addr,val);
}

void DivPlatformMSM6295::poke(std::vector<DivRegWrite>& wlist) {
  //for (DivRegWrite& i: wlist) immWrite(i.addr,i.val);
}

void DivPlatformMSM6295::reset() {
  while (!writes.empty()) writes.pop();
  msm->reset();
  msm->ss_w(false);
  if (dumpWrites) {
    addWrite(0xffffffff,0);
  }
  for (int i=0; i<4; i++) {
    chan[i]=DivPlatformMSM6295::Channel();
    chan[i].std.setEngine(parent);
  }
  for (int i=0; i<4; i++) {
    chan[i].vol=8;
    chan[i].outVol=8;
  }

  sampleBank=0;
  rateSel=false;

  delay=0;
}

bool DivPlatformMSM6295::keyOffAffectsArp(int ch) {
  return false;
}

float DivPlatformMSM6295::getPostAmp() {
  return 3.0f;
}

void DivPlatformMSM6295::notifyInsChange(int ins) {
  for (int i=0; i<4; i++) {
    if (chan[i].ins==ins) {
      chan[i].insChanged=true;
    }
  }
}

void DivPlatformMSM6295::notifyInsDeletion(void* ins) {
}

const void* DivPlatformMSM6295::getSampleMem(int index) {
  return index == 0 ? adpcmMem : NULL;
}

size_t DivPlatformMSM6295::getSampleMemCapacity(int index) {
  return index == 0 ? 262144 : 0;
}

size_t DivPlatformMSM6295::getSampleMemUsage(int index) {
  return index == 0 ? adpcmMemLen : 0;
}

void DivPlatformMSM6295::renderSamples() {
  memset(adpcmMem,0,getSampleMemCapacity(0));

  // sample data
  size_t memPos=128*8;
  int sampleCount=parent->song.sampleLen;
  if (sampleCount>128) sampleCount=128;
  for (int i=0; i<sampleCount; i++) {
    DivSample* s=parent->song.sample[i];
    int paddedLen=s->lengthVOX;
    if (memPos>=getSampleMemCapacity(0)) {
      logW("out of ADPCM memory for sample %d!",i);
      break;
    }
    if (memPos+paddedLen>=getSampleMemCapacity(0)) {
      memcpy(adpcmMem+memPos,s->dataVOX,getSampleMemCapacity(0)-memPos);
      logW("out of ADPCM memory for sample %d!",i);
    } else {
      memcpy(adpcmMem+memPos,s->dataVOX,paddedLen);
    }
    s->offVOX=memPos;
    memPos+=paddedLen;
  }
  adpcmMemLen=memPos+256;

  // phrase book
  for (int i=0; i<sampleCount; i++) {
    DivSample* s=parent->song.sample[i];
    int endPos=s->offVOX+s->lengthVOX;
    adpcmMem[i*8]=(s->offVOX>>16)&0xff;
    adpcmMem[1+i*8]=(s->offVOX>>8)&0xff;
    adpcmMem[2+i*8]=(s->offVOX)&0xff;
    adpcmMem[3+i*8]=(endPos>>16)&0xff;
    adpcmMem[4+i*8]=(endPos>>8)&0xff;
    adpcmMem[5+i*8]=(endPos)&0xff;
  }
}

void DivPlatformMSM6295::setFlags(unsigned int flags) {
  switch (flags) {
    case 0:
      chipClock=4000000/4;
      break;
    case 1:
      chipClock=4224000/4;
      break;
    case 2:
      chipClock=4000000;
      break;
    case 3:
      chipClock=4224000;
      break;
    case 4:
      chipClock=COLOR_NTSC;
      break;
    case 5:
      chipClock=COLOR_NTSC/2.0;
      break;
    case 6:
      chipClock=COLOR_NTSC*2.0/7.0;
      break;
    case 7:
      chipClock=COLOR_NTSC/4.0;
      break;
    case 8:
      chipClock=4000000/2;
      break;
    case 9:
      chipClock=4224000/2;
      break;
    case 10:
      chipClock=875000;
      break;
    case 11:
      chipClock=937500;
      break;
    case 12:
      chipClock=1500000;
      break;
    default:
      chipClock=4000000/4;
      break;
  }
  rate=chipClock/3;
  for (int i=0; i<4; i++) {
    isMuted[i]=false;
    oscBuf[i]->rate=rate/22;
  }
}

int DivPlatformMSM6295::init(DivEngine* p, int channels, int sugRate, unsigned int flags) {
  parent=p;
  adpcmMem=new unsigned char[getSampleMemCapacity(0)];
  adpcmMemLen=0;
  iface.adpcmMem=adpcmMem;
  iface.sampleBank=0;
  dumpWrites=false;
  skipRegisterWrites=false;
  updateOsc=0;
  for (int i=0; i<4; i++) {
    isMuted[i]=false;
    oscBuf[i]=new DivDispatchOscBuffer;
  }
  msm=new msm6295_core(iface);
  setFlags(flags);
  reset();
  return 4;
}

void DivPlatformMSM6295::quit() {
  for (int i=0; i<4; i++) {
    delete oscBuf[i];
  }
  delete msm;
  delete[] adpcmMem;
}

DivPlatformMSM6295::~DivPlatformMSM6295() {
}
