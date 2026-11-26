#pragma once

#include "nes/core_bridge.h"
#include "wizardry_inventory.h"
#include <algorithm>
#include <utility>

struct ConsoleItemLine {
    int x,y,width,height;
    bool selected;
    std::wstring label;
};

class ConsoleInventory {
    std::vector<WizardryPartyMember> inventory;
    size_t itemMember=0,itemRow=0,itemRecipient=0;
    bool actionMenu=false,itemDelete=false,itemDeleteYes=false,itemRight=false;
    unsigned itemAction=WIIZ_ITEM_GIVE;
    std::wstring notice;

    template<class Read,class Transfer>
    bool bulk(size_t member,size_t other,bool collect,Read read,Transfer transfer) {
        const auto size=inventory.size();
        if(size<2){notice=L"相手がいません";return true;}
        const auto destination=collect?member:other;
        size_t moved=0;
        bool stopped=false;
        for(size_t source=0;source<size&&!stopped;++source) {
            if(source==destination||(!collect&&source!=member))continue;
            for(size_t row=0;row<inventory[source].items.size()&&inventory[destination].items.size()<8;) {
                const auto flags=inventory[source].items[row].flags;
                if((flags&0xc0)||(collect&&!(flags&0x20))){++row;continue;}
                if(transfer(source,row,destination)!=WIIZ_ITEM_MOVED){stopped=true;break;}
                ++moved;
                auto updated=read();
                if(updated.size()!=size)return false;
                inventory=std::move(updated);
            }
        }
        itemRow=inventory[member].items.size()+(collect?0:1);
        if(moved)notice=std::to_wstring(moved)+(stopped?L"個移動。残りは移動できません":L"個 移動しました");
        else if(stopped)notice=L"現在の状態では移動できません";
        else if(inventory[destination].items.size()==8)notice=L"移動先の所持品がいっぱいです";
        else notice=L"移動できる品がありません";
        return true;
    }

public:
    bool open=false;

    bool show(std::vector<WizardryPartyMember> items) {
        *this={};
        inventory=std::move(items);
        itemRecipient=inventory.size()>1?1:0;
        return open=!inventory.empty();
    }

    template<class Read,class Transfer,class Edit>
    bool key(unsigned bits,int x,int y,Read read,Transfer transfer,Edit edit) {
        if(!open)return false;
        const auto size=inventory.size();
        auto change=[&](size_t current,size_t count,unsigned left,unsigned right) {
            return (current+count+((bits&right)!=0)-((bits&left)!=0))%count;
        };
        if(actionMenu) {
            bool activate=(bits&1)!=0;
            if(itemDelete) {
                if(bits&2){itemDelete=false;return true;}
                if(bits&(16|32|64|128))itemDeleteYes=!itemDeleteYes;
                if(x>=44&&x<212&&y>=103&&y<123){itemDeleteYes=x<128;activate=true;}
                if(!activate)return true;
                itemDelete=false;
                if(!itemDeleteYes){actionMenu=false;return true;}
            } else {
                if(bits&2){actionMenu=false;return true;}
                itemAction=change(itemAction,5,64,128);
                if(x>=48&&x<208&&y>=65&&y<160){itemAction=(y-65)/19;activate=true;}
                if(!activate)return true;
                if(itemAction==WIIZ_ITEM_DELETE){itemDelete=true;itemDeleteYes=false;return true;}
            }
            const auto member=itemRight?itemRecipient:itemMember;
            const auto other=itemRight?itemMember:itemRecipient;
            const auto action=static_cast<WiizItemAction>(itemAction);
            actionMenu=false;
            if(action==WIIZ_ITEM_GIVE&&size<2){notice=L"渡す相手がいません";return true;}
            const auto result=action==WIIZ_ITEM_GIVE?transfer(member,itemRow,other):
                edit(member,itemRow,action);
            if(result==WIIZ_ITEM_MOVED) {
                auto updated=read();
                if(updated.size()!=size)return open=false;
                inventory=std::move(updated);
                const auto remaining=inventory[member].items.size();
                if(action==WIIZ_ITEM_MOVE_UP)--itemRow;
                if(action==WIIZ_ITEM_MOVE_DOWN)++itemRow;
                itemRow=remaining?std::min(itemRow,remaining-1):0;
                notice=action==WIIZ_ITEM_GIVE?L"アイテムを渡しました":L"所持品を更新しました";
            } else if(result==WIIZ_ITEM_FULL)notice=L"相手の所持品がいっぱいです";
            else if(result==WIIZ_ITEM_EQUIPPED)notice=L"装備中・呪いの品は移動・削除できません";
            else if(result==WIIZ_ITEM_CURSED)notice=L"呪われた装備は外せません";
            else if(result==WIIZ_ITEM_INELIGIBLE)notice=L"このアイテムは装備できません";
            else notice=L"現在の状態では操作できません";
            return true;
        }
        if(bits&2)return open=false;
        const bool wasRight=itemRight;
        if((bits&32)||(y>=0&&y<194&&x>=4&&x<124))itemRight=false;
        if(size>1&&((bits&16)||(y>=0&&y<194&&x>=132&&x<252)))itemRight=true;
        if(itemRight!=wasRight){itemRow=0;notice.clear();}
        auto& member=itemRight?itemRecipient:itemMember;
        const auto other=itemRight?itemMember:itemRecipient;
        if(size>2&&(bits&(256|512))) {
            auto next=change(member,size,256,512);
            member=next==other?change(next,size,256,512):next;
            itemRow=0;notice.clear();
        }
        const auto count=inventory[member].items.size();
        itemRow=change(itemRow,count+2,64,128);
        const int left=itemRight?132:4;
        const bool itemTap=x>=left&&x<left+120&&y>=58&&y<58+17*static_cast<int>(count);
        const bool bulkTap=x>=8&&x<248&&y>=194&&y<222;
        if(itemTap)itemRow=static_cast<size_t>((y-58)/17);
        if(bulkTap)itemRow=count+(y>=208);
        if((bits&1)||itemTap||bulkTap) {
            if(itemRow>=count) {
                if(!bulk(member,other,itemRow==count,read,transfer))return open=false;
            } else {actionMenu=true;itemAction=WIIZ_ITEM_GIVE;notice.clear();}
        }
        return open;
    }

    std::vector<ConsoleItemLine> lines() const {
        std::vector<ConsoleItemLine> result;
        auto line=[&](int x,int y,int w,int h,bool selected,std::wstring label) {
            result.push_back({x,y,w,h,selected,std::move(label)});
        };
        if(inventory.empty()) {
            line(8,8,240,18,false,L"パーティーがいません");
        } else {
            auto pane=[&](int x,size_t index,bool sender) {
                const auto& member=inventory[index];
                const bool focused=sender!=itemRight;
                line(x,7,120,19,focused,std::to_wstring(index+1)+L"番");
                line(x,29,120,14,false,std::wstring(member.name.begin(),member.name.end()));
                line(x,44,120,12,false,std::to_wstring(member.items.size())+L" / 8");
                if(member.items.empty())line(x,58,120,16,false,L"アイテムなし");
                for(size_t i=0;i<member.items.size();++i) {
                    const auto& item=member.items[i];
                    const int y=58+static_cast<int>(i)*17;
                    const auto name=item.glyphs.empty()?item.fallback:std::wstring(item.glyphs.begin(),item.glyphs.end());
                    line(x,y,94,16,focused&&i==itemRow,name);
                    if(!item.status.empty())line(x+95,y,24,16,false,item.status.find(L"呪い")!=std::wstring::npos?L"呪":
                        item.status.find(L"装備中")!=std::wstring::npos?L"装":L"？");
                }
            };
            pane(4,itemMember,true);
            if(inventory.size()>1)pane(132,itemRecipient,false);
            else line(132,7,120,19,false,L"受取側なし");
        }
        if(itemDelete) {
            line(44,83,168,20,false,L"削除しますか？");
            line(44,103,84,20,itemDeleteYes,L"はい");
            line(128,103,84,20,!itemDeleteYes,L"いいえ");
            line(8,223,240,16,false,L"A: 決定  B: 取消");
        } else if(actionMenu) {
            const auto& member=inventory[itemRight?itemRecipient:itemMember];
            const wchar_t* actions[]={(member.items[itemRow].flags&128)?L"装備解除":L"装備",L"渡す",L"削除",L"上に移動",L"下に移動"};
            for(unsigned i=0;i<5;++i)line(48,65+i*19,160,19,itemAction==i,actions[i]);
            line(8,223,240,16,false,L"上下:操作 A:決定 B:戻る");
        } else {
            if(!inventory.empty()) {
                const size_t count=inventory[itemRight?itemRecipient:itemMember].items.size();
                line(8,194,240,14,itemRow==count,L"未識別品を集める");
                line(8,208,240,14,itemRow==count+1,L"未装備品を相手に渡す");
            }
            line(8,223,240,16,false,notice.empty()?L"左右:欄 L/R:人 A:操作 B:戻る":notice);
        }
        return result;
    }
};
