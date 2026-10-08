// Oregon Trail 5th Edition compatibility layer for Windows 10/11 (proxy winmm.dll).
// Runs the stock GOG OT5.EXE windowed at 2x: scales the main window, selected dialogs and Bink
// movies, remaps mouse input, stops the desktop resolution switch, keeps the game running when
// it loses focus, and fixes the Save/Load working-directory bug. 32-bit, CRT-free.
#include <windows.h>
#include <stdarg.h>
#include <commdlg.h>
#include "winmm_names.h"

/* Forwarding table for every winmm export (see stubs.S / gen_stubs.py). Filled from the
   system's own winmm.dll at load, so no copy of it has to ship next to the game. */
void* g_winmm[WINMM_NEXPORTS];

static int   g_scale = 2;
// (CLIP_PAD retired: padding the clip let glyphs land outside the area the game erases ->
// ghost digits. Text now uses exact 2x native advances instead, see nativeDx.)
#define CLIP_PAD 0
static DWORD g_pid   = 0;
static char  g_logpath[MAX_PATH];

static int g_logOn;
static void L(const char* fmt, ...) {
    if(!g_logOn) return;
    char buf[1030];                                  /* wvsprintfA writes at most 1024 chars */ va_list ap; va_start(ap,fmt); int n=wvsprintfA(buf,fmt,ap); va_end(ap);
    buf[n]='\r'; buf[n+1]='\n';
    HANDLE hf=CreateFileA(g_logpath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(hf!=INVALID_HANDLE_VALUE){SetFilePointer(hf,0,NULL,FILE_END);DWORD w;WriteFile(hf,buf,n+2,&w,NULL);CloseHandle(hf);}
}

typedef HWND (WINAPI *CreateWindowExA_t)(DWORD,LPCSTR,LPCSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID);
typedef BOOL (WINAPI *BitBlt_t)(HDC,int,int,int,int,HDC,int,int,DWORD);
typedef int  (WINAPI *StretchDIBits_t)(HDC,int,int,int,int,int,int,int,int,const void*,const BITMAPINFO*,UINT,DWORD);
typedef BOOL (WINAPI *StretchBlt_t)(HDC,int,int,int,int,HDC,int,int,int,int,DWORD);
typedef BOOL (WINAPI *ScreenToClient_t)(HWND,LPPOINT);
typedef BOOL (WINAPI *GetClientRect_t)(HWND,LPRECT);
typedef BOOL (WINAPI *PeekMessageA_t)(LPMSG,HWND,UINT,UINT,UINT);
typedef int  (WINAPI *SetStretchBltMode_t)(HDC,int);
typedef int  (WINAPI *GetClipBox_t)(HDC,LPRECT);
typedef int  (WINAPI *SelectClipRgn_t)(HDC,HRGN);
typedef int  (WINAPI *GetRgnBox_t)(HRGN,LPRECT);
typedef HRGN (WINAPI *CreateRectRgn_t)(int,int,int,int);
typedef BOOL (WINAPI *DeleteObject_t)(HGDIOBJ);
typedef BOOL (WINAPI *ExtTextOutA_t)(HDC,int,int,UINT,const RECT*,LPCSTR,UINT,const INT*);
typedef BOOL (WINAPI *TextOutA_t)(HDC,int,int,LPCSTR,int);
typedef HGDIOBJ (WINAPI *GetCurrentObject_t)(HDC,UINT);
typedef int  (WINAPI *GetObjectA_t)(HGDIOBJ,int,LPVOID);
typedef HFONT (WINAPI *CreateFontIndirectA_t)(const LOGFONTA*);
typedef HGDIOBJ (WINAPI *SelectObject_t)(HDC,HGDIOBJ);
typedef int  (WINAPI *IntersectClipRect_t)(HDC,int,int,int,int);
typedef int  (WINAPI *ExcludeClipRect_t)(HDC,int,int,int,int);
typedef BOOL (WINAPI *InvalidateRect_t)(HWND,const RECT*,BOOL);
typedef LRESULT (WINAPI *SendMessageA_t)(HWND,UINT,WPARAM,LPARAM);
typedef HWND (WINAPI *GetActiveWindow_t)(void);
typedef BOOL (WINAPI *ShowWindow_t)(HWND,int);
typedef BOOL (WINAPI *CloseWindow_t)(HWND);
typedef BOOL (WINAPI *SetWindowPos_t)(HWND,HWND,int,int,int,int,UINT);
typedef BOOL (WINAPI *PostMessageA_t)(HWND,UINT,WPARAM,LPARAM);

static CreateWindowExA_t   o_CreateWindowExA;
static BitBlt_t            o_BitBlt;
static StretchDIBits_t     o_StretchDIBits;
static StretchBlt_t        p_StretchBlt;
static ScreenToClient_t    o_ScreenToClient;
static GetClientRect_t     o_GetClientRect;
static PeekMessageA_t      o_PeekMessageA;
static SetStretchBltMode_t p_SetStretchBltMode;
static GetClipBox_t        p_GetClipBox;
static SelectClipRgn_t     o_SelectClipRgn;
static GetRgnBox_t         p_GetRgnBox;
static CreateRectRgn_t     p_CreateRectRgn;
static DeleteObject_t      p_DeleteObject;
static ExtTextOutA_t       o_ExtTextOutA;
static TextOutA_t          o_TextOutA;
static ExtTextOutA_t       p_ExtTextOutA;
static GetCurrentObject_t  p_GetCurrentObject;
static GetObjectA_t        p_GetObjectA;
static CreateFontIndirectA_t p_CreateFontIndirectA;
static SelectObject_t      p_SelectObject;
static IntersectClipRect_t o_IntersectClipRect;
static ExcludeClipRect_t   o_ExcludeClipRect;
static InvalidateRect_t    o_InvalidateRect;
static SendMessageA_t      o_SendMessageA;
static GetActiveWindow_t   o_GetActiveWindow;
static ShowWindow_t        o_ShowWindow;
static CloseWindow_t       o_CloseWindow;
static SetWindowPos_t      o_SetWindowPos;
static PostMessageA_t      o_PostMessageA;
static WNDPROC             g_diaryOrigProc;
static HBRUSH              g_parchBrush;
static LRESULT CALLBACK DiaryProc(HWND,UINT,WPARAM,LPARAM);
static WNDPROC             g_movieOrigProc;
static WNDPROC             g_mainOrigProc;
static LRESULT CALLBACK MainProc(HWND,UINT,WPARAM,LPARAM);
static int isOurTop(HWND h);
static int isMinimizeCmd(UINT msg,WPARAM wp);
static LRESULT CALLBACK MovieProc(HWND,UINT,WPARAM,LPARAM);
static LRESULT CALLBACK DlgProc(HWND,UINT,WPARAM,LPARAM);

static int isGameWin(HWND w){ if(!w) return 0; DWORD pid=0; GetWindowThreadProcessId(w,&pid); return pid==g_pid; }
// MECCDClass dialogs we scale, keyed by exact title. Others (character creation, shops,
// the trail map — whose title varies per trail) stay native by design.
static int isScaledDlgName(const char* t){
    return lstrcmpA(t,"Conversation")==0 || lstrcmpA(t,"Diary")==0 || lstrcmpA(t,"Guidebook")==0
        || lstrcmpA(t,"Health")==0 || lstrcmpA(t,"Purchase Inventory")==0
        || lstrcmpA(t,"Oregon Trail 5 Movie Window")==0;
}
// Scaled dialogs are tracked by HANDLE (decided once, at creation) because some have no stable
// title — the trail map's title is the trail's name ("Montgomery", ...).
static HWND g_scaled[32]; static int g_nScaled; static HWND g_mainWnd;
static void untrack(HWND w){ int i; for(i=0;i<g_nScaled;i++) if(g_scaled[i]==w){ g_scaled[i]=g_scaled[--g_nScaled]; return; } }
static void track(HWND w){ int i; untrack(w);
    for(i=0;i<g_nScaled;i++) if(!IsWindow(g_scaled[i])){ g_scaled[i]=g_scaled[--g_nScaled]; i--; }
    if(g_nScaled<32) g_scaled[g_nScaled++]=w; }
static int isScaledWin(HWND w){
    int i; if(!w) return 0;
    if(w==g_mainWnd) return 1;
    for(i=0;i<g_nScaled;i++) if(g_scaled[i]==w) return 1;
    return 0;
}
static int isMainDC(HDC hdc){ return isScaledWin(WindowFromDC(hdc)); }

static HWND WINAPI h_CreateWindowExA(DWORD ex,LPCSTR cls,LPCSTR name,DWORD style,
        int x,int y,int w,int h,HWND par,HMENU menu,HINSTANCE inst,LPVOID p){
    int named = (name && ((ULONG_PTR)name>>16)!=0);
    int strcls = (cls && ((ULONG_PTR)cls>>16)!=0);
    int isMain = strcls && lstrcmpA(cls,"MECCWClass")==0;
    int isDlg  = strcls && lstrcmpA(cls,"MECCDClass")==0;
    int scaleDlg = 0;
    if(isMain){
        // Scale the CLIENT x2 and add a caption so the window can be dragged/minimized.
        RECT nc; nc.left=0; nc.top=0; nc.right=0; nc.bottom=0; AdjustWindowRectEx(&nc,style,TRUE,ex);
        int cw=(w-(nc.right-nc.left))*g_scale, ch=(h-(nc.bottom-nc.top))*g_scale;
        style |= WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
        RECT r; r.left=0; r.top=0; r.right=cw; r.bottom=ch; AdjustWindowRectEx(&r,style,TRUE,ex);
        w=r.right-r.left; h=r.bottom-r.top;
        x=(GetSystemMetrics(SM_CXSCREEN)-w)/2; y=(GetSystemMetrics(SM_CYSCREEN)-h)/2; if(y<0) y=0; if(x<0) x=0;
        L("main create client=%dx%d win=%dx%d",cw,ch,w,h);
    } else if(isDlg){
        RECT nc; nc.left=0; nc.top=0; nc.right=0; nc.bottom=0; AdjustWindowRectEx(&nc,style,FALSE,ex);
        int ncw=nc.right-nc.left, nch=nc.bottom-nc.top;
        // Whitelisted titles, or any "full-screen" dialog (640-wide client: trail map, movies).
        scaleDlg = (named && isScaledDlgName(name)) || ((w-ncw)==640 && (h-nch)>=480);
        if(scaleDlg){
            int nw=(w-ncw)*g_scale+ncw, nh=(h-nch)*g_scale+nch;
            x -= (nw-w)/2; y -= (nh-h)/2; if(y<0) y=0; w=nw; h=nh;
        }
    } else if((style & WS_CHILD) && par && isScaledWin(par)){
        // True child control (e.g. the diary's edit box) of a scaled window: scale pos+size to
        // match. WS_CHILD guard is essential — owned pop-ups (Quit, Prologue) also have the main
        // window as owner but must NOT be resized, or their native-size content strands in a corner.
        x*=g_scale; y*=g_scale; w*=g_scale; h*=g_scale;
    }
    HWND hw = o_CreateWindowExA(ex,cls,name,style,x,y,w,h,par,menu,inst,p);
    if(!hw) return hw;
    if(isMain){ g_mainWnd=hw; g_mainOrigProc=(WNDPROC)(ULONG_PTR)SetWindowLongA(hw,GWL_WNDPROC,(LONG)(ULONG_PTR)MainProc); }
    else if(scaleDlg) track(hw); else untrack(hw);      // untrack: a recycled HWND must not inherit scaling
    // Focus filter on every dialog except the movie window (MovieProc has its own).
    if(isDlg && !(named && lstrcmpA(name,"Oregon Trail 5 Movie Window")==0)){
        SetPropA(hw,"ot5sc_pend",(HANDLE)0);
        SetPropA(hw,"ot5sc_proc",(HANDLE)(ULONG_PTR)SetWindowLongA(hw,GWL_WNDPROC,(LONG)(ULONG_PTR)DlgProc));
    }
    // Subclass the Diary so its edit control paints parchment instead of stark white.
    if(isDlg && named && lstrcmpA(name,"Diary")==0)
        g_diaryOrigProc=(WNDPROC)(ULONG_PTR)SetWindowLongA(hw,GWL_WNDPROC,(LONG)(ULONG_PTR)DiaryProc);
    // Movie window: Bink only blits dirty rects, so erase the whole (doubled) client to black.
    if(isDlg && named && lstrcmpA(name,"Oregon Trail 5 Movie Window")==0)
        g_movieOrigProc=(WNDPROC)(ULONG_PTR)SetWindowLongA(hw,GWL_WNDPROC,(LONG)(ULONG_PTR)MovieProc);
    return hw;
}
static unsigned g_cBlit,g_cRgn,g_cInv,g_cPeek; static DWORD g_hbNext;
static void heartbeat(void){
    DWORD now=GetTickCount(); if(!g_hbNext){ g_hbNext=now+10000; return; }
    if((int)(now-g_hbNext)<0) return; g_hbNext=now+10000;
    L("t=%u hb blits=%u paintrgn=%u inval=%u peeks=%u fg=%x iconic=%d",(unsigned)now,g_cBlit,g_cRgn,g_cInv,g_cPeek,
      (int)(ULONG_PTR)GetForegroundWindow(), g_mainWnd?(int)IsIconic(g_mainWnd):-1);
    g_cBlit=g_cRgn=g_cInv=g_cPeek=0;
}
static BOOL WINAPI h_BitBlt(HDC hd,int x,int y,int cx,int cy,HDC hs,int x1,int y1,DWORD rop){
    g_cBlit++;
    if(isMainDC(hd)){ if(p_SetStretchBltMode)p_SetStretchBltMode(hd,COLORONCOLOR);
        return p_StretchBlt(hd,x*g_scale,y*g_scale,cx*g_scale,cy*g_scale,hs,x1,y1,cx,cy,rop); }
    return o_BitBlt(hd,x,y,cx,cy,hs,x1,y1,rop);
}
static int WINAPI h_StretchDIBits(HDC hd,int x,int y,int cx,int cy,int sx,int sy,int sw,int sh,
        const void* bits,const BITMAPINFO* bmi,UINT usage,DWORD rop){
    g_cBlit++;
    if(isMainDC(hd)){ if(p_SetStretchBltMode)p_SetStretchBltMode(hd,COLORONCOLOR);
        return o_StretchDIBits(hd,x*g_scale,y*g_scale,cx*g_scale,cy*g_scale,sx,sy,sw,sh,bits,bmi,usage,rop); }
    return o_StretchDIBits(hd,x,y,cx,cy,sx,sy,sw,sh,bits,bmi,usage,rop);
}
// Select a g_scale-sized copy of the DC's current font. Returns old font (restore), *made=created font (delete).
static HGDIOBJ scaleFont(HDC hdc, HFONT* made){
    *made = NULL;
    if(!p_GetCurrentObject||!p_GetObjectA||!p_CreateFontIndirectA||!p_SelectObject) return NULL;
    HGDIOBJ cur = p_GetCurrentObject(hdc, 6 /*OBJ_FONT*/);
    if(!cur) return NULL;
    LOGFONTA lf;
    if(p_GetObjectA(cur, sizeof(lf), &lf) == 0) return NULL;
    lf.lfHeight *= g_scale;
    if(lf.lfWidth) lf.lfWidth *= g_scale;
    HFONT big = p_CreateFontIndirectA(&lf);
    if(!big) return NULL;
    HGDIOBJ old = p_SelectObject(hdc, big);
    *made = big;
    return old;
}
// Text must occupy EXACTLY 2x its native footprint: the game sizes clip/erase areas from native
// font metrics, and a x2 font is never exactly 2x as wide. Overrun -> clipped last digit;
// padding the clip instead -> ghost digits the game never erases. So: measure the string in
// the native font (target = 2x that), let GDI lay out the big font naturally (keeps the
// font's own spacing), then spread the small difference evenly across the glyphs.
static int fitDx(HDC hdc,LPCSTR s,UINT n,int targetW,INT* out){
    UINT i; int sum=0,diff,step,rem;
    if(!s||!n||n>512) return 0;
    for(i=0;i<n;i++){ INT w=0; unsigned char ch=(unsigned char)s[i];
        if(!GetCharWidth32A(hdc,ch,ch,&w)) return 0; out[i]=w; sum+=w; }
    diff=targetW-sum; if(diff==0) return 1;
    step=diff/(int)n; rem=diff-step*(int)n;               // rem has the sign of diff
    for(i=0;i<n;i++){ out[i]+=step; if(rem>0){out[i]++;rem--;} else if(rem<0){out[i]--;rem++;} if(out[i]<1) out[i]=1; }
    return 1;
}
static BOOL WINAPI h_ExtTextOutA(HDC hdc,int x,int y,UINT opt,const RECT* rc,LPCSTR s,UINT n,const INT* dx){
    if(isMainDC(hdc)){
        INT dxb[512]; const INT* pdx = NULL; SIZE nat; int haveNat=0;
        if(dx && n<=512){ UINT i; for(i=0;i<n;i++) dxb[i]=dx[i]*g_scale; pdx=dxb; }
        else if(!dx && s && n && n<=512) haveNat=GetTextExtentPoint32A(hdc,s,(int)n,&nat);
        HFONT made; HGDIOBJ old = scaleFont(hdc,&made);
        if(haveNat && old && fitDx(hdc,s,n,nat.cx*g_scale,dxb)) pdx=dxb;
        RECT r2; const RECT* prc = rc;
        if(rc){ r2.left=rc->left*g_scale; r2.top=rc->top*g_scale; r2.right=rc->right*g_scale; r2.bottom=rc->bottom*g_scale; prc=&r2; }
        BOOL r = o_ExtTextOutA(hdc, x*g_scale, y*g_scale, opt, prc, s, n, pdx);
        if(old){ p_SelectObject(hdc,old); if(made) p_DeleteObject(made); }
        return r;
    }
    return o_ExtTextOutA(hdc,x,y,opt,rc,s,n,dx);
}
static BOOL WINAPI h_TextOutA(HDC hdc,int x,int y,LPCSTR s,int n){
    if(isMainDC(hdc)){
        INT dxb[512]; SIZE nat; int haveNat = (s && n>0 && n<=512) ? GetTextExtentPoint32A(hdc,s,n,&nat) : 0;
        HFONT made; HGDIOBJ old = scaleFont(hdc,&made);
        int fit = (haveNat && old && p_ExtTextOutA) ? fitDx(hdc,s,(UINT)n,nat.cx*g_scale,dxb) : 0;
        BOOL r = fit ? p_ExtTextOutA(hdc,x*g_scale,y*g_scale,0,NULL,s,(UINT)n,dxb)
                     : o_TextOutA(hdc, x*g_scale, y*g_scale, s, n);
        if(old){ p_SelectObject(hdc,old); if(made) p_DeleteObject(made); }
        return r;
    }
    return o_TextOutA(hdc,x,y,s,n);
}
// ---- vector drawing (trail line on the travel map, markers, boxes) ----------------------
// The game draws these straight onto the window DC in native coords. Unscaled they land at
// half position/thickness and the scaled repaint regions erase pieces of them.
typedef BOOL (WINAPI *MoveToEx_t)(HDC,int,int,LPPOINT);
typedef BOOL (WINAPI *LineTo_t)(HDC,int,int);
typedef BOOL (WINAPI *Poly_t)(HDC,const POINT*,int);
typedef BOOL (WINAPI *Box_t)(HDC,int,int,int,int);
typedef BOOL (WINAPI *RoundRect_t)(HDC,int,int,int,int,int,int);
static MoveToEx_t o_MoveToEx; static LineTo_t o_LineTo; static Poly_t o_Polyline, o_Polygon;
static Box_t o_Rectangle, o_Ellipse; static RoundRect_t o_RoundRect;
static HGDIOBJ scalePen(HDC hdc,HPEN* made){
    *made=NULL; LOGPEN lp;
    HGDIOBJ cur=GetCurrentObject(hdc,OBJ_PEN); if(!cur) return NULL;
    if(GetObjectA(cur,sizeof(lp),&lp)!=sizeof(lp)) return NULL;
    if(lp.lopnStyle==PS_NULL) return NULL;
    int w=lp.lopnWidth.x; if(w<1) w=1;
    HPEN big=CreatePen(lp.lopnStyle,w*g_scale,lp.lopnColor); if(!big) return NULL;
    *made=big; return SelectObject(hdc,big);
}
static void unscalePen(HDC hdc,HGDIOBJ old,HPEN made){ if(old) SelectObject(hdc,old); if(made) DeleteObject(made); }
static BOOL WINAPI h_MoveToEx(HDC hdc,int x,int y,LPPOINT prev){
    if(isMainDC(hdc)){ BOOL r=o_MoveToEx(hdc,x*g_scale,y*g_scale,prev);
        if(r&&prev){ prev->x/=g_scale; prev->y/=g_scale; } return r; }
    return o_MoveToEx(hdc,x,y,prev);
}
static BOOL WINAPI h_LineTo(HDC hdc,int x,int y){
    if(isMainDC(hdc)){ HPEN made; HGDIOBJ old=scalePen(hdc,&made);
        BOOL r=o_LineTo(hdc,x*g_scale,y*g_scale); unscalePen(hdc,old,made); return r; }
    return o_LineTo(hdc,x,y);
}
static BOOL polyScaled(Poly_t fn,HDC hdc,const POINT* pts,int n){
    if(isMainDC(hdc) && pts && n>0 && n<=256){ POINT b[256]; int i;
        for(i=0;i<n;i++){ b[i].x=pts[i].x*g_scale; b[i].y=pts[i].y*g_scale; }
        HPEN made; HGDIOBJ old=scalePen(hdc,&made); BOOL r=fn(hdc,b,n); unscalePen(hdc,old,made); return r; }
    return fn(hdc,pts,n);
}
static BOOL WINAPI h_Polyline(HDC hdc,const POINT* p,int n){ return polyScaled(o_Polyline,hdc,p,n); }
static BOOL WINAPI h_Polygon (HDC hdc,const POINT* p,int n){ return polyScaled(o_Polygon ,hdc,p,n); }
static BOOL boxScaled(Box_t fn,HDC hdc,int l,int t,int r,int b){
    if(isMainDC(hdc)){ HPEN made; HGDIOBJ old=scalePen(hdc,&made);
        BOOL ok=fn(hdc,l*g_scale,t*g_scale,r*g_scale,b*g_scale); unscalePen(hdc,old,made); return ok; }
    return fn(hdc,l,t,r,b);
}
static BOOL WINAPI h_Rectangle(HDC hdc,int l,int t,int r,int b){ return boxScaled(o_Rectangle,hdc,l,t,r,b); }
static BOOL WINAPI h_Ellipse  (HDC hdc,int l,int t,int r,int b){ return boxScaled(o_Ellipse  ,hdc,l,t,r,b); }
static BOOL WINAPI h_RoundRect(HDC hdc,int l,int t,int r,int b,int w,int h){
    if(isMainDC(hdc)){ HPEN made; HGDIOBJ old=scalePen(hdc,&made);
        BOOL ok=o_RoundRect(hdc,l*g_scale,t*g_scale,r*g_scale,b*g_scale,w*g_scale,h*g_scale); unscalePen(hdc,old,made); return ok; }
    return o_RoundRect(hdc,l,t,r,b,w,h);
}
// Region painting: the travel map's live trail is a red-brushed region painted with PaintRgn
// in native coords. Scale the whole region shape (not just its box) by g_scale.
typedef BOOL (WINAPI *RgnOp_t)(HDC,HRGN);
static RgnOp_t o_PaintRgn, o_InvertRgn;
static HRGN scaleRgn(HRGN r){
    static BYTE buf[32768];                       /* game is single-threaded for drawing */
    DWORD n=GetRegionData(r,0,NULL); if(!n || n>sizeof(buf)) return NULL;
    if(!GetRegionData(r,n,(RGNDATA*)buf)) return NULL;
    XFORM x; x.eM11=(FLOAT)g_scale; x.eM12=0; x.eM21=0; x.eM22=(FLOAT)g_scale; x.eDx=0; x.eDy=0;
    return ExtCreateRegion(&x,n,(RGNDATA*)buf);
}
static BOOL rgnScaled(RgnOp_t fn,HDC hdc,HRGN r){
    if(r && isMainDC(hdc)){ HRGN s=scaleRgn(r); if(s){ BOOL ok=fn(hdc,s); DeleteObject(s); return ok; } }
    return fn(hdc,r);
}
static BOOL WINAPI h_PaintRgn (HDC hdc,HRGN r){ g_cRgn++; return rgnScaled(o_PaintRgn ,hdc,r); }
static BOOL WINAPI h_InvertRgn(HDC hdc,HRGN r){ return rgnScaled(o_InvertRgn,hdc,r); }
// Game clips each element to native coords; scale the clip region to match the scaled blits.
static int WINAPI h_SelectClipRgn(HDC hdc, HRGN hrgn){
    if(hrgn && p_GetRgnBox && p_CreateRectRgn && p_DeleteObject){
        if(isScaledWin(WindowFromDC(hdc))){
            RECT b;
            if(p_GetRgnBox(hrgn,&b)){
                HRGN sr = p_CreateRectRgn(b.left*g_scale-CLIP_PAD,b.top*g_scale,b.right*g_scale+CLIP_PAD,b.bottom*g_scale);
                int r = o_SelectClipRgn(hdc,sr);
                p_DeleteObject(sr);
                return r;
            }
        }
    }
    return o_SelectClipRgn(hdc,hrgn);
}
// Game clips the person/animation redraw with a native-coord rect via IntersectClipRect; scale it.
static int WINAPI h_IntersectClipRect(HDC hdc,int l,int t,int r,int b){
    if(isMainDC(hdc))
        return o_IntersectClipRect(hdc,l*g_scale-CLIP_PAD,t*g_scale,r*g_scale+CLIP_PAD,b*g_scale);
    return o_IntersectClipRect(hdc,l,t,r,b);
}
static int WINAPI h_ExcludeClipRect(HDC hdc,int l,int t,int r,int b){
    if(isMainDC(hdc)) return o_ExcludeClipRect(hdc,l*g_scale,t*g_scale,r*g_scale,b*g_scale);
    return o_ExcludeClipRect(hdc,l,t,r,b);
}
// Invalidated region drives BeginPaint's clip; game invalidates native rects. Scale so x2 blits fit.
static BOOL WINAPI h_InvalidateRect(HWND hwnd,const RECT* rc,BOOL e){
    g_cInv++;
    if(rc && isScaledWin(hwnd)){
        RECT s; s.left=rc->left*g_scale; s.top=rc->top*g_scale; s.right=rc->right*g_scale; s.bottom=rc->bottom*g_scale;
        return o_InvalidateRect(hwnd,&s,e); }
    return o_InvalidateRect(hwnd,rc,e);
}
// Game hit-tests via WM_MOUSE* message lParam (client coords). Scale those down.
static BOOL WINAPI h_PeekMessageA(LPMSG m,HWND hwnd,UINT mn,UINT mx,UINT rm){
    BOOL r = o_PeekMessageA(m,hwnd,mn,mx,rm);
    g_cPeek++; heartbeat();
    if(r && m && m->message>=0x200 && m->message<=0x20D && isScaledWin(m->hwnd)){
        long x=(short)(m->lParam & 0xFFFF)/g_scale;
        long y=(short)((m->lParam>>16) & 0xFFFF)/g_scale;
        m->lParam=(LPARAM)(((y & 0xFFFF)<<16)|(x & 0xFFFF));
    }
    return r;
}
static BOOL WINAPI h_ScreenToClient(HWND hwnd,LPPOINT pt){
    BOOL r=o_ScreenToClient(hwnd,pt);
    if(r&&pt&&isScaledWin(hwnd)){ pt->x/=g_scale; pt->y/=g_scale; }
    return r;
}
// Game logic lives in native 640x480 coords; report the native client size so
// edge-zone checks (town pan right/bottom: x > width-N) line up with /2 cursor coords.
static BOOL WINAPI h_GetClientRect(HWND hwnd,LPRECT rc){
    BOOL r=o_GetClientRect(hwnd,rc);
    if(r&&rc&&isScaledWin(hwnd)){ rc->right/=g_scale; rc->bottom/=g_scale; }
    return r;
}
// Child controls of scaled windows get x2 rects but keep their native font — double the
// font the game assigns via WM_SETFONT so combo/edit text matches the scaled layout.
static HFONT dupFont2x(HFONT f){
    static HFONT seen[16], made[16]; static int n;
    int i; for(i=0;i<n;i++) if(seen[i]==f) return made[i];
    LOGFONTA lf;
    if(!p_GetObjectA || p_GetObjectA(f,sizeof(lf),&lf)==0) return f;
    lf.lfHeight*=g_scale; if(lf.lfWidth) lf.lfWidth*=g_scale;
    HFONT big = p_CreateFontIndirectA ? p_CreateFontIndirectA(&lf) : NULL;
    if(!big) return f;
    if(n<16){ seen[n]=f; made[n]=big; n++; }
    return big;
}
static LRESULT WINAPI h_SendMessageA(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(isOurTop(hwnd) && isMinimizeCmd(msg,wp)){ L("blocked sent SC_MINIMIZE"); return 0; }
    if(msg==WM_SETFONT && wp){
        HWND par=GetParent(hwnd);
        if((GetWindowLongA(hwnd,GWL_STYLE)&WS_CHILD) && par && isScaledWin(par))
            wp=(WPARAM)dupFont2x((HFONT)wp);
    }
    return o_SendMessageA(hwnd,msg,wp,lp);
}
// The diary's edit control paints on a white background that clashes with the parchment
// page art; answer WM_CTLCOLOREDIT/STATIC with a parchment brush instead.
static LRESULT CALLBACK DiaryProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORSTATIC){
        if(!g_parchBrush) g_parchBrush=CreateSolidBrush(RGB(242,231,206));
        SetBkColor((HDC)wp,RGB(242,231,206));
        return (LRESULT)g_parchBrush;
    }
    return CallWindowProcA(g_diaryOrigProc,hwnd,msg,wp,lp);
}

// ---- keep the game running when it is not the active app -------------------------------
// OT5 was written for exclusive fullscreen: it polls GetActiveWindow() and stalls (no travel,
// black movies, silent audio) — and can minimize itself — whenever activation is not on one of
// its windows. On Win11 that also happens spuriously when one of its own dialogs closes while
// the owner is disabled, leaving it stalled until clicked. Report the main window as active
// and hide app-deactivation from it.
static HWND WINAPI h_GetActiveWindow(void){
    HWND r=o_GetActiveWindow();
    if(r) return r;
    if(g_nScaled && IsWindow(g_scaled[g_nScaled-1]) && IsWindowVisible(g_scaled[g_nScaled-1])) return g_scaled[g_nScaled-1];
    if(g_mainWnd && IsWindow(g_mainWnd)) return g_mainWnd;
    return r;
}
// The game minimizes ITSELF ~15ms after WM_ACTIVATE(inactive) — exclusive-fullscreen habit.
// Observed: repeated self-minimizes, each followed by long stretches of zero draws (trail
// "stalls", black movies on restore). It uses CloseWindow (ShowWindow hook never fired). Block every
// programmatic route; a user minimize via the caption button goes through DefWindowProc
// WM_SYSCOMMAND from a real click, which we allow (see MainProc).
static int isOurTop(HWND h){ return h && (h==g_mainWnd || isScaledWin(h)); }
static BOOL WINAPI h_CloseWindow(HWND hwnd){
    if(isOurTop(hwnd)){ L("blocked CloseWindow(minimize) hwnd=%x",(int)(ULONG_PTR)hwnd); return TRUE; }
    return o_CloseWindow(hwnd);
}
static BOOL WINAPI h_SetWindowPos(HWND hwnd,HWND after,int x,int y,int cx,int cy,UINT f){
    if(isOurTop(hwnd) && (f & SWP_HIDEWINDOW)){ L("blocked SetWindowPos(HIDE) hwnd=%x",(int)(ULONG_PTR)hwnd); f&=~SWP_HIDEWINDOW; }
    if(hwnd==g_mainWnd && !(f & SWP_NOMOVE) && x<=-32000){ L("blocked SetWindowPos(offscreen)"); f|=SWP_NOMOVE|SWP_NOSIZE; }
    return o_SetWindowPos(hwnd,after,x,y,cx,cy,f);
}
static int isMinimizeCmd(UINT msg,WPARAM wp){ return msg==WM_SYSCOMMAND && ((wp&0xFFF0)==SC_MINIMIZE); }
static BOOL WINAPI h_PostMessageA(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(isOurTop(hwnd) && isMinimizeCmd(msg,wp)){ L("blocked posted SC_MINIMIZE"); return TRUE; }
    return o_PostMessageA(hwnd,msg,wp,lp);
}
static BOOL WINAPI h_ShowWindow(HWND hwnd,int cmd){
    if(hwnd==g_mainWnd && (cmd==SW_MINIMIZE||cmd==SW_SHOWMINIMIZED||cmd==SW_SHOWMINNOACTIVE||cmd==SW_FORCEMINIMIZE)){
        L("blocked self-minimize cmd=%d",cmd); return TRUE; }
    return o_ShowWindow(hwnd,cmd);
}
// Swallow deactivation AND the paired reactivation: letting the TRUE through made the game
// run its resume handler, which restarts the background music over a playing movie.
static int g_pendMain, g_pendMovie;
// "Never tell it": the game must never learn it lost focus. Deactivation messages are dropped,
// and the matching re-activation is dropped too (else its resume handler restarts music over a
// movie / re-runs init). The very first activation of each window passes through.
// Returns 1 if the message was consumed.
static int filterFocus(const char* who,int* pend,HWND hwnd,UINT msg,WPARAM wp){
    switch(msg){
    case WM_ACTIVATEAPP:
        if(!wp){ *pend|=1; return 1; }
        if(*pend&1){ *pend&=~1; L("%s: dropped paired ACTIVATEAPP",who); return 1; } return 0;
    case WM_ACTIVATE:
        if(LOWORD(wp)==WA_INACTIVE){ *pend|=2; return 1; }
        if(*pend&2){ *pend&=~2; L("%s: dropped paired ACTIVATE",who); return 1; } return 0;
    case WM_NCACTIVATE:
        return 0;                                   /* caption paint only; DefWindowProc handles it */
    case WM_KILLFOCUS:
        *pend|=4; return 1;
    case WM_SETFOCUS:
        if(*pend&4){ *pend&=~4; return 1; } return 0;
    }
    return 0;
}
static void actLog(const char* w,UINT msg,WPARAM wp){
    if(msg==WM_ACTIVATE||msg==WM_ACTIVATEAPP||msg==WM_SETFOCUS||msg==WM_KILLFOCUS||msg==WM_SHOWWINDOW||msg==WM_SIZE)
        L("t=%u %s msg=%x wp=%x fg=%x", (unsigned)GetTickCount(), w, msg, (int)wp, (int)(ULONG_PTR)GetForegroundWindow());
}
static LRESULT CALLBACK MainProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    actLog("main",msg,wp);
    if(isMinimizeCmd(msg,wp) && lp==0){ L("blocked SC_MINIMIZE (no mouse pos)"); return 0; }
    if(msg==WM_NCACTIVATE) return CallWindowProcA(g_mainOrigProc,hwnd,msg,wp,lp);
    if(filterFocus("main",&g_pendMain,hwnd,msg,wp)) return (msg==WM_ACTIVATEAPP||msg==WM_ACTIVATE)?0:DefWindowProcA(hwnd,msg,wp,lp);
    return CallWindowProcA(g_mainOrigProc,hwnd,msg,wp,lp);
}
static LRESULT CALLBACK MovieProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    // WM_ACTIVATEAPP goes to EVERY top-level window of the app; the movie window's handler
    // hides itself on deactivation, leaving only the blacked-out main window ("black movie").
    actLog("movie",msg,wp);
    if(filterFocus("movie",&g_pendMovie,hwnd,msg,wp)) return (msg==WM_ACTIVATEAPP||msg==WM_ACTIVATE)?0:DefWindowProcA(hwnd,msg,wp,lp);
    if(msg==WM_DESTROY) g_pendMovie=0;
    if(msg==WM_ERASEBKGND){ RECT r; GetClientRect(hwnd,&r); FillRect((HDC)wp,&r,(HBRUSH)GetStockObject(BLACK_BRUSH)); return 1; }
    return CallWindowProcA(g_movieOrigProc,hwnd,msg,wp,lp);
}
// Every other MECCDClass dialog (Health, shops, Guidebook, ...) is its own top-level window and
// gets its own deactivation messages — with Health open, clicking away paused the game even
// though the main window never heard about it. Same filter; per-window state lives in props
// because many dialogs can be alive at once and HWNDs get recycled.
static LRESULT CALLBACK DlgProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    WNDPROC orig=(WNDPROC)GetPropA(hwnd,"ot5sc_proc");
    int pend=(int)(ULONG_PTR)GetPropA(hwnd,"ot5sc_pend");
    actLog("dlg",msg,wp);
    int drop=filterFocus("dlg",&pend,hwnd,msg,wp);
    SetPropA(hwnd,"ot5sc_pend",(HANDLE)(ULONG_PTR)pend);
    if(drop) return (msg==WM_ACTIVATEAPP||msg==WM_ACTIVATE)?0:DefWindowProcA(hwnd,msg,wp,lp);
    if(msg==WM_NCDESTROY){ RemovePropA(hwnd,"ot5sc_pend"); RemovePropA(hwnd,"ot5sc_proc"); }
    return CallWindowProcA(orig,hwnd,msg,wp,lp);
}
// ---- Save/Load dialog must not move the working directory ------------------------------
// OREGON5.INI locates data with a RELATIVE path (rsrcpath=.\Data). The common file dialog
// changes the process CWD to wherever the user browses (e.g. Documents). After that every
// open of .\Data\* fails: Bink movies show a black window, and Oregon5.Dat resource reads
// run on an invalid handle -> the game's unchecked chunk parser (0x452FF7) spins forever
// (hard freeze, 100% CPU). Original-game bug; pin the CWD around the dialog.
typedef BOOL (WINAPI *FileDlg_t)(LPOPENFILENAMEA);
static FileDlg_t o_GetSaveFileNameA, o_GetOpenFileNameA;
static BOOL fileDlg(FileDlg_t fn,LPOPENFILENAMEA ofn,const char* who){
    char cwd[MAX_PATH]; DWORD n=GetCurrentDirectoryA(MAX_PATH,cwd);
    if(ofn) ofn->Flags |= OFN_NOCHANGEDIR;
    BOOL r=fn(ofn);
    if(n && n<MAX_PATH){ char now[MAX_PATH]; DWORD m=GetCurrentDirectoryA(MAX_PATH,now);
        if(!m || m>=MAX_PATH || lstrcmpiA(now,cwd)!=0){ SetCurrentDirectoryA(cwd); L("%s: cwd was moved to %s, restored %s",who,now,cwd); } }
    return r;
}
static BOOL WINAPI h_GetSaveFileNameA(LPOPENFILENAMEA o){ return fileDlg(o_GetSaveFileNameA,o,"SaveDlg"); }
static BOOL WINAPI h_GetOpenFileNameA(LPOPENFILENAMEA o){ return fileDlg(o_GetOpenFileNameA,o,"OpenDlg"); }
// ---- binkw32 movie scaling ----------------------------------------------------------
// Bink loads ddraw.dll dynamically and presents movies via DirectDraw at native size,
// bypassing every GDI hook. Deny it ddraw and it falls back to its GDI path
// (DIBSection + StretchBlt to the movie window DC), which we can scale like the rest.
typedef HMODULE (WINAPI *LoadLibraryA_t)(LPCSTR);
static LoadLibraryA_t o_bink_LoadLibraryA;
static StretchBlt_t   o_bink_StretchBlt;
static int nameHasDDraw(const char* s){
    if(!s) return 0;
    for(; *s; s++){
        if((s[0]=='d'||s[0]=='D')&&(s[1]=='d'||s[1]=='D')&&(s[2]=='r'||s[2]=='R')&&
           (s[3]=='a'||s[3]=='A')&&(s[4]=='w'||s[4]=='W')) return 1;
    }
    return 0;
}
static HMODULE WINAPI h_bink_LoadLibraryA(LPCSTR name){
    if(nameHasDDraw(name)){ L("bink LoadLibraryA(%s) denied -> GDI fallback", name); return NULL; }
    return o_bink_LoadLibraryA(name);
}
static BOOL WINAPI h_bink_StretchBlt(HDC hd,int x,int y,int w,int h,HDC hs,int sx,int sy,int sw,int sh,DWORD rop){
    if(isMainDC(hd))
        return o_bink_StretchBlt(hd,x*g_scale,y*g_scale,w*g_scale,h*g_scale,hs,sx,sy,sw,sh,rop);
    return o_bink_StretchBlt(hd,x,y,w,h,hs,sx,sy,sw,sh,rop);
}
// --------------------------------------------------------------------------------------

static void* HookIAT(HMODULE mod,const char* dll,const char* fn,void* newfn){
    BYTE* base=(BYTE*)mod; IMAGE_DOS_HEADER* dos=(IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);
    DWORD r=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress; if(!r)return NULL;
    IMAGE_IMPORT_DESCRIPTOR* imp=(IMAGE_IMPORT_DESCRIPTOR*)(base+r);
    for(;imp->Name;imp++){ if(lstrcmpiA((const char*)(base+imp->Name),dll)!=0)continue;
        IMAGE_THUNK_DATA* oft=(IMAGE_THUNK_DATA*)(base+(imp->OriginalFirstThunk?imp->OriginalFirstThunk:imp->FirstThunk));
        IMAGE_THUNK_DATA* ft=(IMAGE_THUNK_DATA*)(base+imp->FirstThunk);
        for(;oft->u1.AddressOfData;oft++,ft++){ if(oft->u1.Ordinal&IMAGE_ORDINAL_FLAG)continue;
            IMAGE_IMPORT_BY_NAME* ibn=(IMAGE_IMPORT_BY_NAME*)(base+oft->u1.AddressOfData);
            if(lstrcmpA((const char*)ibn->Name,fn)!=0)continue;
            void* o=(void*)ft->u1.Function; DWORD old;
            VirtualProtect(&ft->u1.Function,4,PAGE_READWRITE,&old); ft->u1.Function=(ULONG_PTR)newfn; VirtualProtect(&ft->u1.Function,4,old,&old);
            return o; } }
    return NULL;
}
// The stock EXE switches the desktop to 640x480x16 on startup and activation. Pretend it worked
// and leave the desktop alone; the game then runs in a window like the patched EXE did.
typedef LONG (WINAPI *ChangeDisplaySettingsA_t)(DEVMODEA*,DWORD);
static ChangeDisplaySettingsA_t o_ChangeDisplaySettingsA;
static LONG WINAPI h_ChangeDisplaySettingsA(DEVMODEA* dm,DWORD f){
    if(dm) L("ChangeDisplaySettingsA %dx%dx%d flags=%x -> ignored",(int)dm->dmPelsWidth,(int)dm->dmPelsHeight,(int)dm->dmBitsPerPel,(int)f);
    else   L("ChangeDisplaySettingsA(NULL) flags=%x -> ignored",(int)f);
    return DISP_CHANGE_SUCCESSFUL;
}
static int loadRealWinmm(void){
    char p[MAX_PATH]; UINT n=GetSystemDirectoryA(p,MAX_PATH); int i,miss=0; HMODULE m;
    if(!n||n>MAX_PATH-12) return -1;
    p[n]=92; lstrcpyA(p+n+1,"winmm.dll");
    m=LoadLibraryA(p); if(!m) return -1;
    for(i=0;i<WINMM_NEXPORTS;i++){ g_winmm[i]=(void*)GetProcAddress(m,g_winmmNames[i]); if(!g_winmm[i]) miss++; }
    return miss;
}
static void Init(HINSTANCE self){
    int winmmMissing=loadRealWinmm();
    GetModuleFileNameA(self,g_logpath,MAX_PATH); int i=lstrlenA(g_logpath);
    while(i>0&&g_logpath[i-1]!='\\')i--; lstrcpyA(g_logpath+i,"scaler.log");
    // Logging is opt-in: create an empty scaler.log next to the game to enable it.
    if(GetFileAttributesA(g_logpath)!=INVALID_FILE_ATTRIBUTES){
        HANDLE t=CreateFileA(g_logpath,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL); if(t!=INVALID_HANDLE_VALUE)CloseHandle(t);
        g_logOn=1;
    }
    g_pid=GetCurrentProcessId(); L("scaler v4 scale=%d pid=%d",g_scale,g_pid); L("real winmm: %d of %d exports unresolved",winmmMissing,WINMM_NEXPORTS);
    HMODULE exe=GetModuleHandleA(NULL), gdi=GetModuleHandleA("gdi32.dll");
    p_StretchBlt=(StretchBlt_t)GetProcAddress(gdi,"StretchBlt");
    p_SetStretchBltMode=(SetStretchBltMode_t)GetProcAddress(gdi,"SetStretchBltMode");
    p_GetClipBox=(GetClipBox_t)GetProcAddress(gdi,"GetClipBox");
    p_GetRgnBox=(GetRgnBox_t)GetProcAddress(gdi,"GetRgnBox");
    p_CreateRectRgn=(CreateRectRgn_t)GetProcAddress(gdi,"CreateRectRgn");
    p_DeleteObject=(DeleteObject_t)GetProcAddress(gdi,"DeleteObject");
    p_GetCurrentObject=(GetCurrentObject_t)GetProcAddress(gdi,"GetCurrentObject");
    p_GetObjectA=(GetObjectA_t)GetProcAddress(gdi,"GetObjectA");
    p_CreateFontIndirectA=(CreateFontIndirectA_t)GetProcAddress(gdi,"CreateFontIndirectA");
    p_SelectObject=(SelectObject_t)GetProcAddress(gdi,"SelectObject");
    p_ExtTextOutA=(ExtTextOutA_t)GetProcAddress(gdi,"ExtTextOutA");
    o_ChangeDisplaySettingsA=(ChangeDisplaySettingsA_t)HookIAT(exe,"user32.dll","ChangeDisplaySettingsA",h_ChangeDisplaySettingsA);
    L("CDS hook=%x",(int)(ULONG_PTR)o_ChangeDisplaySettingsA);
    o_CreateWindowExA=(CreateWindowExA_t)HookIAT(exe,"user32.dll","CreateWindowExA",h_CreateWindowExA);
    o_ScreenToClient =(ScreenToClient_t) HookIAT(exe,"user32.dll","ScreenToClient", h_ScreenToClient);
    o_GetClientRect  =(GetClientRect_t)  HookIAT(exe,"user32.dll","GetClientRect",  h_GetClientRect);
    o_SendMessageA   =(SendMessageA_t)   HookIAT(exe,"user32.dll","SendMessageA",   h_SendMessageA);
    o_GetActiveWindow=(GetActiveWindow_t)HookIAT(exe,"user32.dll","GetActiveWindow",h_GetActiveWindow);
    o_ShowWindow     =(ShowWindow_t)     HookIAT(exe,"user32.dll","ShowWindow",     h_ShowWindow);
    o_CloseWindow    =(CloseWindow_t)    HookIAT(exe,"user32.dll","CloseWindow",    h_CloseWindow);
    o_SetWindowPos   =(SetWindowPos_t)   HookIAT(exe,"user32.dll","SetWindowPos",   h_SetWindowPos);
    o_PostMessageA   =(PostMessageA_t)   HookIAT(exe,"user32.dll","PostMessageA",   h_PostMessageA);
    o_PeekMessageA   =(PeekMessageA_t)   HookIAT(exe,"user32.dll","PeekMessageA",   h_PeekMessageA);
    o_SelectClipRgn  =(SelectClipRgn_t)  HookIAT(exe,"gdi32.dll", "SelectClipRgn",  h_SelectClipRgn);
    o_ExtTextOutA    =(ExtTextOutA_t)    HookIAT(exe,"gdi32.dll", "ExtTextOutA",    h_ExtTextOutA);
    o_TextOutA       =(TextOutA_t)       HookIAT(exe,"gdi32.dll", "TextOutA",       h_TextOutA);
    o_IntersectClipRect=(IntersectClipRect_t)HookIAT(exe,"gdi32.dll","IntersectClipRect",h_IntersectClipRect);
    o_ExcludeClipRect=(ExcludeClipRect_t)HookIAT(exe,"gdi32.dll", "ExcludeClipRect", h_ExcludeClipRect);
    o_InvalidateRect =(InvalidateRect_t) HookIAT(exe,"user32.dll","InvalidateRect",  h_InvalidateRect);
    o_BitBlt        =(BitBlt_t)         HookIAT(exe,"gdi32.dll", "BitBlt",         h_BitBlt);
    o_StretchDIBits =(StretchDIBits_t)  HookIAT(exe,"gdi32.dll", "StretchDIBits",  h_StretchDIBits);
    o_PaintRgn =(RgnOp_t)    HookIAT(exe,"gdi32.dll","PaintRgn", h_PaintRgn);
    o_InvertRgn=(RgnOp_t)    HookIAT(exe,"gdi32.dll","InvertRgn",h_InvertRgn);
    o_MoveToEx =(MoveToEx_t) HookIAT(exe,"gdi32.dll","MoveToEx", h_MoveToEx);
    o_LineTo   =(LineTo_t)   HookIAT(exe,"gdi32.dll","LineTo",   h_LineTo);
    o_Polyline =(Poly_t)     HookIAT(exe,"gdi32.dll","Polyline", h_Polyline);
    o_Polygon  =(Poly_t)     HookIAT(exe,"gdi32.dll","Polygon",  h_Polygon);
    o_Rectangle=(Box_t)      HookIAT(exe,"gdi32.dll","Rectangle",h_Rectangle);
    o_Ellipse  =(Box_t)      HookIAT(exe,"gdi32.dll","Ellipse",  h_Ellipse);
    o_RoundRect=(RoundRect_t)HookIAT(exe,"gdi32.dll","RoundRect",h_RoundRect);
    o_GetSaveFileNameA=(FileDlg_t)HookIAT(exe,"comdlg32.dll","GetSaveFileNameA",h_GetSaveFileNameA);
    o_GetOpenFileNameA=(FileDlg_t)HookIAT(exe,"comdlg32.dll","GetOpenFileNameA",h_GetOpenFileNameA);
    // Also pin the CWD to the game folder at startup (shortcuts/launchers may start us elsewhere).
    { char d[MAX_PATH]; int k; GetModuleFileNameA(NULL,d,MAX_PATH); k=lstrlenA(d); while(k>0&&d[k-1]!=92)k--; if(k>0){ d[k-1]=0; SetCurrentDirectoryA(d); } }
    L("dlg hooks save=%x open=%x",(int)(ULONG_PTR)o_GetSaveFileNameA,(int)(ULONG_PTR)o_GetOpenFileNameA);
    HMODULE bink=GetModuleHandleA("binkw32.dll");
    if(bink){
        o_bink_LoadLibraryA=(LoadLibraryA_t)HookIAT(bink,"kernel32.dll","LoadLibraryA",h_bink_LoadLibraryA);
        o_bink_StretchBlt  =(StretchBlt_t)  HookIAT(bink,"gdi32.dll",   "StretchBlt",  h_bink_StretchBlt);
        L("bink hooks LL=%x SB=%x",(int)(ULONG_PTR)o_bink_LoadLibraryA,(int)(ULONG_PTR)o_bink_StretchBlt);
    }
    L("hooks CW=%x S2C=%x BB=%x SDIB=%x SBlt=%x",(int)(ULONG_PTR)o_CreateWindowExA,(int)(ULONG_PTR)o_ScreenToClient,(int)(ULONG_PTR)o_BitBlt,(int)(ULONG_PTR)o_StretchDIBits,(int)(ULONG_PTR)p_StretchBlt);
}
BOOL WINAPI _DllMainCRTStartup(HINSTANCE h,DWORD reason,LPVOID r){
    if(reason==DLL_PROCESS_ATTACH){ DisableThreadLibraryCalls(h); Init(h); } return TRUE;
}
