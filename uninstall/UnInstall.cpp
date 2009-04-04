#include <windows.h>
#include <objbase.h>
#include <tchar.h>
#include <strsafe.h>
#include "plugin.hpp"
#include "memory.h"
#define realloc my_realloc
#include "eplugin.cpp"
#include "farcolor.hpp"
#include "farkeys.hpp"
#include "farlang.h"
#include "registry.cpp"
#include "uninstall.hpp"

#ifdef FARAPI18
#  define SetStartupInfo SetStartupInfoW
#  define GetPluginInfo GetPluginInfoW
#  define OpenPlugin OpenPluginW
#endif

#ifdef FARAPI17
int WINAPI GetMinFarVersion(void)
{
  return MAKEFARVERSION(1,75,2555);
}
#endif
#ifdef FARAPI18
int WINAPI GetMinFarVersion(void)
{
  return FARMANAGERVERSION;
}
int WINAPI GetMinFarVersionW(void)
{
  return FARMANAGERVERSION;
}
#endif

void WINAPI SetStartupInfo(const struct PluginStartupInfo *psInfo)
{
  Info = *psInfo;
  FSF = *psInfo->FSF;
  Info.FSF = &FSF;
  InitHeap();
  StringCchCopy(PluginRootKey,ARRAYSIZE(PluginRootKey),Info.RootKey);
  StringCchCat(PluginRootKey,ARRAYSIZE(PluginRootKey),_T("\\UnInstall"));
  ReadRegistry();
}

void WINAPI GetPluginInfo(struct PluginInfo *Info)
{
  static const TCHAR *PluginMenuStrings[1];
  PluginMenuStrings[0] = GetMsg(MPlugIn);
  Info -> StructSize = sizeof(*Info);
  Info -> PluginMenuStrings = PluginMenuStrings;
  if (Opt.WhereWork & 2)
    Info -> Flags |= PF_EDITOR;
  if (Opt.WhereWork & 1)
    Info -> Flags |= PF_VIEWER;
  Info -> PluginMenuStringsNumber = ARRAYSIZE(PluginMenuStrings);
}

void ResizeDialog(HANDLE hDlg) {
  GetConsoleScreenBufferInfo(hStdout, &csbiInfo);
  #define MAXITEMS (csbiInfo.dwSize.Y-7)
  int s = ((ListSize>0) && (ListSize<MAXITEMS) ? ListSize : (ListSize>0 ? MAXITEMS : 0));
  #undef MAXITEMS
  SMALL_RECT NewPos = { 2, 1, csbiInfo.dwSize.X - 7, s + 2 };
  SMALL_RECT OldPos;
  Info.SendDlgMessage(hDlg,DM_GETITEMPOSITION,LIST_BOX,reinterpret_cast<LONG_PTR>(&OldPos));
  if (NewPos.Right!=OldPos.Right || NewPos.Bottom!=OldPos.Bottom) {
    COORD coord;
    coord.X = csbiInfo.dwSize.X - 4;
    coord.Y = s + 4;
    Info.SendDlgMessage(hDlg,DM_RESIZEDIALOG,0,reinterpret_cast<LONG_PTR>(&coord));
    coord.X = -1;
    coord.Y = -1;
    Info.SendDlgMessage(hDlg,DM_MOVEDIALOG,TRUE,reinterpret_cast<LONG_PTR>(&coord));
    Info.SendDlgMessage(hDlg,DM_SETITEMPOSITION,LIST_BOX,reinterpret_cast<LONG_PTR>(&NewPos));
  }
}

static LONG_PTR WINAPI DlgProc(HANDLE hDlg,int Msg,int Param1,LONG_PTR Param2)
{
  static TCHAR Filter[MAX_PATH];
  static TCHAR spFilter[MAX_PATH];
  static FarListTitles ListTitle;

  switch(Msg)
  {
    case DN_RESIZECONSOLE:
    {
      Info.SendDlgMessage(hDlg,DM_ENABLEREDRAW,FALSE,0);
      ResizeDialog(hDlg);
      Info.SendDlgMessage(hDlg,DM_ENABLEREDRAW,TRUE,0);
    }
    return TRUE;

    case DMU_UPDATE:
    {
      int OldPos = static_cast<int>(Info.SendDlgMessage(hDlg,DM_LISTGETCURPOS,LIST_BOX,0));

      if (Param1)
        UpDateInfo();

      ListSize = 0;
      int NewPos = -1;
      for (int i=0;i<nCount;i++)
      {
        const TCHAR* DispName = p[i].Keys[DisplayName];
        if (strstri(DispName,Filter)) //��� ��� ॣ���� � OEM ����஢��
        {
          FLI[i].Flags &= ~LIF_HIDDEN;
          //��� ��� ॣ���� - � ����஢�� ANSI
          if (NewPos == -1 && strstri(DispName,Filter) == DispName)
            NewPos = i;
          ListSize++;
        }
        else
          FLI[i].Flags |= LIF_HIDDEN;
      }
      if (NewPos == -1) NewPos = OldPos;

      Info.SendDlgMessage(hDlg,DM_ENABLEREDRAW,FALSE,0);

      Info.SendDlgMessage(hDlg,DM_LISTSET,LIST_BOX,reinterpret_cast<LONG_PTR>(&FL));

      FSF.sprintf(spFilter,GetMsg(MFilter),Filter,ListSize,nCount);
      ListTitle.Title = spFilter;
      ListTitle.TitleLen = lstrlen(spFilter);
      Info.SendDlgMessage(hDlg,DM_LISTSETTITLES,LIST_BOX,reinterpret_cast<LONG_PTR>(&ListTitle));

      ResizeDialog(hDlg);

      struct FarListPos FLP;
      FLP.SelectPos = NewPos;
      FLP.TopPos = -1;
      Info.SendDlgMessage(hDlg,DM_LISTSETCURPOS,LIST_BOX,reinterpret_cast<LONG_PTR>(&FLP));

      Info.SendDlgMessage(hDlg,DM_ENABLEREDRAW,TRUE,0);
    }
    break;

    case DN_INITDIALOG:
    {
      StringCchCopy(Filter,ARRAYSIZE(Filter),_T(""));
      ListTitle.Bottom = const_cast<TCHAR*>(GetMsg(MBottomLine));
      ListTitle.BottomLen = lstrlen(GetMsg(MBottomLine));

      //�����ࠨ������ ��� ࠧ���� ���᮫�
      Info.SendDlgMessage(hDlg,DM_ENABLEREDRAW,FALSE,0);
      ResizeDialog(hDlg);
      Info.SendDlgMessage(hDlg,DM_ENABLEREDRAW,TRUE,0);
      //������塞 ������
      Info.SendDlgMessage(hDlg,DMU_UPDATE,1,0);
    }
    break;

    case DN_MOUSECLICK:
    {
      if (Param1 == LIST_BOX) {
        MOUSE_EVENT_RECORD *mer = (MOUSE_EVENT_RECORD *)Param2;
        if (mer->dwButtonState == FROM_LEFT_1ST_BUTTON_PRESSED) {
          // find list on-screen coords (excluding frame and border)
          SMALL_RECT list_rect;
          Info.SendDlgMessage(hDlg, DM_GETDLGRECT, 0, reinterpret_cast<LONG_PTR>(&list_rect));
          list_rect.Left += 2;
          list_rect.Top += 1;
          list_rect.Right -= 2;
          list_rect.Bottom -= 1;
          if ((mer->dwEventFlags == 0) && (mer->dwMousePosition.X > list_rect.Left) && (mer->dwMousePosition.X < list_rect.Right) && (mer->dwMousePosition.Y > list_rect.Top) && (mer->dwMousePosition.Y < list_rect.Bottom)) {
            DlgProc(hDlg, DN_KEY, LIST_BOX, KEY_ENTER);
            return TRUE;
          }
          // pass message to scrollbar if needed
          if ((mer->dwMousePosition.X == list_rect.Right) && (mer->dwMousePosition.Y > list_rect.Top) && (mer->dwMousePosition.Y < list_rect.Bottom)) return FALSE;
          return TRUE;
        }
      }
    }
    break;

    case DN_KEY:
      switch (Param2)
      {
        case KEY_F8:
        {
          if (ListSize)
          {
            TCHAR DlgText[MAX_PATH + 200];
            FSF.sprintf(DlgText, GetMsg(MConfirm), p[Info.SendDlgMessage(hDlg,DM_LISTGETCURPOS,LIST_BOX,NULL)].Keys[DisplayName]);
            if (EMessage((const TCHAR * const *) DlgText, 0, 2) == 0)
            {
              DeleteEntry(static_cast<int>(Info.SendDlgMessage(hDlg,DM_LISTGETCURPOS,LIST_BOX,NULL)));
              Info.SendDlgMessage(hDlg,DMU_UPDATE,1,0);
            }
          }
        }
        return TRUE;

        case KEY_CTRLR:
        {
          Info.SendDlgMessage(hDlg,DMU_UPDATE,1,0);
        }
        return TRUE;

        case KEY_ENTER:
        case KEY_SHIFTENTER:
        {
          if (ListSize) {
            int pos = static_cast<int>(Info.SendDlgMessage(hDlg,DM_LISTGETCURPOS,LIST_BOX,NULL));
            if (((Param2==KEY_ENTER) && Opt.EnterFunction) || ((Param2==KEY_SHIFTENTER) && !Opt.EnterFunction)) {
              ExecuteEntry(pos, true);
              Info.SendDlgMessage(hDlg,DMU_UPDATE,1,0);
            }
            else {
              ExecuteEntry(pos, false);
            }
          }
        }
        return TRUE;

        case KEY_SHIFTINS:
        case KEY_CTRLV:
        {
          TCHAR * bufP = FSF.PasteFromClipboard();
          static TCHAR bufData[MAX_PATH];
          if (bufP)
          {
            StringCchCopy(bufData,ARRAYSIZE(bufData),bufP);
            FSF.DeleteBuffer(bufP);
            unQuote(bufData);
            FSF.LStrlwr(bufData);
            for (int i = lstrlen(bufData); i >= 1; i--)
              for (int j = 0; j < nCount; j++)
                if (strnstri(p[j].Keys[DisplayName],bufData,i))
                {
                  lstrcpyn(Filter,bufData,i+1);
                  Info.SendDlgMessage(hDlg,DMU_UPDATE,0,0);
                  return TRUE;
                }
          }
        }
        return TRUE;

        case KEY_DEL:
        {
          if (lstrlen(Filter) > 0)
          {
            StringCchCopy(Filter,ARRAYSIZE(Filter),_T(""));
            Info.SendDlgMessage(hDlg,DMU_UPDATE,0,0);
          }
        }
        return TRUE;

        case KEY_F3:
        {
          if (ListSize)
          {
            DisplayEntry(static_cast<int>(Info.SendDlgMessage(hDlg,DM_LISTGETCURPOS,LIST_BOX,NULL)));
            Info.SendDlgMessage(hDlg,DM_REDRAW,NULL,NULL);
          }
        }
        return TRUE;

        case KEY_BS:
        {
          if (lstrlen(Filter))
          {
            Filter[lstrlen(Filter)-1] = '\0';
            Info.SendDlgMessage(hDlg,DMU_UPDATE,0,0);
          }
        }
        return TRUE;

        default:
        {
          if (Param2 >= KEY_SPACE && Param2 < KEY_FKEY_BEGIN)
          {
            struct FarListInfo ListInfo;
            Info.SendDlgMessage(hDlg,DM_LISTINFO,LIST_BOX,reinterpret_cast<LONG_PTR>(&ListInfo));
            if ((lstrlen(Filter) < sizeof(Filter)) && ListInfo.ItemsNumber)
            {
              int filterL = lstrlen(Filter);
              Filter[filterL] = FSF.LLower(static_cast<unsigned>(Param2));
              Filter[filterL+1] = '\0';
              Info.SendDlgMessage(hDlg,DMU_UPDATE,0,0);
              return TRUE;
            }
          }
        }
      }
      return FALSE;

    case DN_CTLCOLORDIALOG:
      return Info.AdvControl(Info.ModuleNumber,ACTL_GETCOLOR,(void *)COL_MENUTEXT);

    case DN_CTLCOLORDLGLIST:
      if (Param1 == LIST_BOX)
      {
        FarListColors *Colors = (FarListColors *)Param2;
        int ColorIndex[] =
        {
          COL_MENUBOX,COL_MENUBOX,COL_MENUTITLE,COL_MENUTEXT,
          COL_MENUHIGHLIGHT,COL_MENUBOX,COL_MENUSELECTEDTEXT,
          COL_MENUSELECTEDHIGHLIGHT,COL_MENUSCROLLBAR,COL_MENUDISABLEDTEXT
        };
        int Count = ARRAYSIZE(ColorIndex);
        if (Count > Colors->ColorCount)
          Count = Colors->ColorCount;
        for (int i=0;i<Count;i++)
          Colors->Colors[i] = static_cast<BYTE>(Info.AdvControl(Info.ModuleNumber,ACTL_GETCOLOR,(void *)(ColorIndex[i])));
        return TRUE;
      }
    break;
  }
  return Info.DefDlgProc(hDlg,Msg,Param1,Param2);
}

HANDLE WINAPI OpenPlugin(int /*OpenFrom*/, INT_PTR /*Item*/)
{
  ReadRegistry();
  struct FarDialogItem DialogItems[1];
  ZeroMemory(DialogItems, sizeof(DialogItems));
  p = NULL;
  FLI = NULL;

  hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
  UpDateInfo();

  DialogItems[0].Type = DI_LISTBOX;
  DialogItems[0].Flags = DIF_LISTNOAMPERSAND;
  DialogItems[0].X1 = 2;
  DialogItems[0].Y1 = 1;

#ifdef FARAPI17
  Info.DialogEx(Info.ModuleNumber,-1,-1,0,0,"Contents",DialogItems,ARRAYSIZE(DialogItems),0,0,DlgProc,0);
#endif
#ifdef FARAPI18
  HANDLE h_dlg = Info.DialogInit(Info.ModuleNumber,-1,-1,0,0,L"Contents",DialogItems,ARRAYSIZE(DialogItems),0,0,DlgProc,0);
  if (h_dlg != INVALID_HANDLE_VALUE) {
    Info.DialogRun(h_dlg);
    Info.DialogFree(h_dlg);
  }
#endif
  FLI = (FarListItem *) realloc(FLI, 0);
  p = (KeyInfo *) realloc(p, 0);
  return INVALID_HANDLE_VALUE;
}
