#pragma once

#include "wizardry.h"
#include "../map_direction.h"
#include <array>

template<class Pixel>
void renderNesMap(WiizCore* core,const WizardryFeatures& wizardry,
                  unsigned floor,unsigned targetX,unsigned targetY,std::array<Pixel,256*240>& frame) {
    frame.fill(static_cast<Pixel>(0xff080c12u));
    constexpr int left=28,top=20,cell=10;
    constexpr auto wallColor=static_cast<Pixel>(0xffe0e6eeu);
    const auto& walls=wizardry.floors[floor-1].walls;
    const auto* explored=wiiz_core_map_explored(core);
    for(int y=0;y<20;++y)for(int x=0;x<20;++x) {
        const unsigned i=(floor-1)*400+y*20+x;
        if(explored[i/8]&(1u<<(i%8)))
            for(int row=0;row<cell;++row)
                std::fill_n(frame.begin()+(top+(19-y)*cell+row)*256+left+x*cell,cell,0xff315e49u);
    }
    auto edge=[&](int x,int y,int dx,int dy,unsigned wall) {
        if(!wall)return;
        for(int i=0;i<=cell;++i) {
            if(wall==2&&i>2&&i<8)continue;
            frame[(y+dy*i)*256+x+dx*i]=wallColor;
        }
    };
    for(int y=0;y<20;++y)for(int x=0;x<20;++x) {
        const int px=left+x*cell,py=top+(19-y)*cell;
        const unsigned wall=walls[y*20+x];
        edge(px,py,1,0,wall&3);
        edge(px+cell,py,0,1,(wall>>2)&3);
        edge(px,py+cell,1,0,(wall>>4)&3);
        edge(px,py,0,1,(wall>>6)&3);
    }
    const unsigned position=wiiz_core_map_position(core);
    const unsigned playerX=position&255,playerY=(position>>8)&255;
    if(((position>>16)&255)==floor&&playerX<20&&playerY<20) {
        const int px=left+playerX*cell,py=top+(19-playerY)*cell;
        drawMapDirection(px+5,py+5,3,position>>24,[&](int x,int y) {
            frame[y*256+x]=static_cast<Pixel>(0xfff46b64u);
        });
    }
    const int px=left+targetX*cell,py=top+(19-targetY)*cell;
    for(int i=1;i<cell;++i) {
        constexpr auto cursor=static_cast<Pixel>(0xffffdd5au);
        frame[(py+1)*256+px+i]=cursor;
        frame[(py+cell-1)*256+px+i]=cursor;
        frame[(py+i)*256+px+1]=cursor;
        frame[(py+i)*256+px+cell-1]=cursor;
    }
}
