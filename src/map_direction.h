#pragma once

// Direction is clockwise from north. Draw a filled marker with a pointed tip.
template<class Plot>
void drawMapDirection(int x,int y,int radius,unsigned direction,Plot plot) {
    constexpr int dx[]={0,1,0,-1},dy[]={-1,0,1,0};
    const int forwardX=dx[direction&3],forwardY=dy[direction&3];
    for(int forward=-radius;forward<=radius;++forward) {
        const int halfWidth=(radius-forward)/2;
        for(int side=-halfWidth;side<=halfWidth;++side)
            plot(x+forward*forwardX-side*forwardY,y+forward*forwardY+side*forwardX);
    }
}
