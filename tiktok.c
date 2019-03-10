/*
     TIKTOK - SIMPLE MULTI-STOPWATCH FOR THE USR PILOT
     Copyright (C) 1997-1998 David A Pearson
   
     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published by
     the Free Software Foundation; either version 2 of the license, or 
     (at your option) any later version.
     
     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
     GNU General Public License for more details.
     
     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#pragma pack( 2 )

/* Pilot header files. */

#include <Common.h>
#include <System/SysAll.h>
#include <UI/UIAll.h>

/* Local header files. */

#include "tiktok.h"

/* Defines. */

#define TT_CLOCKS 10

#define IS_TIKTOK_CTRL( e ) ( e->data.ctlSelect.controlID >= ID_ChkFirst && \
                              e->data.ctlSelect.controlID < ( ID_ChkFirst \
                                                             + TT_CLOCKS ) )
    
#define IS_STICKY_CTRL( e ) ( e->data.ctlSelect.controlID >= ID_StkFirst && \
                              e->data.ctlSelect.controlID < ( ID_StkFirst \
                                                             + TT_CLOCKS ) )

#define TT_APP_ID ( (ULong) 'DAP2' )

#define GCCNULL   ( (void *) 0 )    

/* Local structures. */

typedef struct
{
    Boolean bOn     : 1;        /* Is the timer running? */
    Boolean bEmpty  : 1;        /* Is it empty? */
    Boolean bSticky : 1;        /* Is it a persistant timer? */
    ULong   ulStart;            /* When did it start running? */
    Long    lElapsed;           /* How much time has elapsed? */
    Long    lInitial;           /* What was the initial value? */
    Char    sDesc[ 32 ];        /* Short note for the timer. */
} t_TikTok;                     /* Used to hold/save info about a clock. */

typedef struct
{
    VoidHand hndTime;           /* Handle for the timer field. */
    VoidHand hndDesc;           /* Handle for the note field. */
    Boolean  bDirty;            /* Is the timer "dirty"? */
    FieldPtr ptrTime;           /* Pointer to the time field. */
    FieldPtr ptrDesc;           /* Pointer to the note field. */
} t_TikTokCtrl;                 /* Used to track the display control. */

typedef struct
{
    Boolean  bVisAlm   : 1;     /* Visual alarm? */
    Boolean  bQuestion : 1;     /* Ask silly questions? */
    Boolean  bShowTime : 1;     /* Display a clock? */
    Boolean  bMemory   : 1;     /* Do timers have start time memory? */
    Boolean  bAlmStop  : 1;     /* Do alarms stop when they fire? */
    int      iFocused;          /* Focus memory. */
    t_TikTok ttClocks[ TT_CLOCKS ];    
} t_TikTokPrefs;                /* Used to store the app prefs. */

typedef enum
{
    TT_CLR_FOCUSED,             /* Clear the focused timer. */
    TT_CLR_STOPPED,             /* Clear all stopped timers. */
    TT_CLR_ALL                  /* Clear all timers. */
} t_TikTokClear;                /* TikTok clear methods. */

/* Global variables. */
    
t_TikTokPrefs         ttPrefs;                 /* Application preferences. */
t_TikTokCtrl          ttControls[ TT_CLOCKS ]; /* Control details array. */
SystemPreferencesType sysPrefs;                /* User's Pilot preferences. */
ULong                 ulLast = 0L;             /* Last time we drew the display. */
Boolean               bCanUpdate = true;       /* Can we update the screen? */

/* Prototype local functions. */

static Boolean ApplicationHandleEvent( EventPtr );
static Boolean MainFormHandleEvent( EventPtr );
static Boolean PrefsFormHandleEvent( EventPtr );
static void    LoadTikToks( void );
static void    SyncTikToks( void );
static void    ToggleTikTok( int );
static void    StopTikTok( int );
static void    StartTikTok( int );
static void    ToggleSticky( int );
static void    RefreshTikTok( void );
static void    FormatTimer( char *, Long );
static void    ICat( UInt, char * );
static void    ZeroPadICat( UInt, char * );
static void    SaveTikToks( void );
static void    ClearTikToks( t_TikTokClear, Boolean );
static VoidPtr Id2Ptr( Word );
static void    DefaultTheDescription( int );
static void    DecorateForm( void );
static Boolean TxtFieldHasFocus( void );
static int     FocusedTikTok( void );
static Word    GetFocusID( void );
static Boolean IsTimeFieldDirty( int );
static void    SetTimeFldEditable( int, Boolean );
static Long    ParseTimeField( int, Boolean * );
static void    StuffTikTok( const char * );
static void    PutWordToFld( Word, Word, Word, Boolean );
static void    SetFieldText( Word, const char *, Word );
static void    ShowTime( void );
static ULong   CalcNextAlarm( int * );
static void    SetAlarm( void );
static void    RaiseAlarm( int );

/*
 */
    
DWord PilotMain( Word cmd, Ptr cmdPBP, Word launchFlags )
{
    if ( cmd == sysAppLaunchCmdNormalLaunch )
    {
        short     err;
        EventType e;
        Boolean   bExit = false;

        /* Get the user's preferences for their Pilot, we want this
           so we can know their desired date and time format. */
        PrefGetPreferences( &sysPrefs );

        LoadTikToks();
        
        FrmGotoForm( ID_FrmTikTok );

        while ( !bExit )
        {
            EvtGetEvent( &e, bCanUpdate ? 1 : -1 ); /* Note the 1, we want nilEvents. */

            if ( SysHandleEvent( &e ) )
            {
                continue;
            }

            if ( MenuHandleEvent( GCCNULL, &e, &err ) )
            {
                continue;
            }

            if ( ApplicationHandleEvent( &e ) )
            {
                continue;
            }

            FrmDispatchEvent( &e );

            if ( e.eType == appStopEvent )
            {
                SaveTikToks();
                SetAlarm();
                FrmCloseAllForms();
                bExit = true;
            }
        }

        return( 0 );
    }
    else if ( cmd == sysAppLaunchCmdAlarmTriggered )
    {
        SetAlarm();
        return( 0 );
    }
    else if ( cmd == sysAppLaunchCmdDisplayAlarm )
    {
        RaiseAlarm( ( (SysDisplayAlarmParamType *) cmdPBP )->ref );
        return( 0 );
    }

    return( sysErrParamErr );
}

/*
 */

static Boolean ApplicationHandleEvent( EventPtr e )
{
    Boolean bUsed = false;
    
    if ( e->eType == frmLoadEvent )
    {
        Word    wForm  = e->data.frmLoad.formID;
        FormPtr ptrFrm = FrmInitForm( wForm );

        FrmSetActiveForm( ptrFrm );

        switch ( wForm )
        {
            
            case ID_FrmTikTok :
                FrmSetEventHandler( FrmGetFormPtr( e->data.frmLoad.formID ), MainFormHandleEvent );
                bUsed = true;
                break;
            
            case ID_FrmOptions :
                FrmSetEventHandler( FrmGetFormPtr( e->data.frmLoad.formID ), PrefsFormHandleEvent );
                bUsed = true;
                break;
        }
    }

    return( bUsed );
}

/*
 */

static Boolean MainFormHandleEvent( EventPtr e )
{
    Boolean bUsed = true;
    Word    wFocused;

    switch ( e->eType )
    {
        case frmOpenEvent :
            
            FrmDrawForm( FrmGetActiveForm() );
            SyncTikToks();
            RefreshTikTok();
            DecorateForm();
            if ( ttPrefs.iFocused != -1 )
            {
                FrmSetFocus( FrmGetActiveForm(), FrmGetObjectIndex( FrmGetActiveForm(), ID_TxtFirst + ttPrefs.iFocused ) );
            }
            break;
            
        case ctlSelectEvent :
            
            if ( IS_TIKTOK_CTRL( e ) )
            {
                ToggleTikTok( e->data.ctlSelect.controlID - ID_ChkFirst );
                RefreshTikTok();
            }
            else if ( IS_STICKY_CTRL( e ) )
            {
                ToggleSticky( e->data.ctlSelect.controlID - ID_StkFirst );
            }
            else
            {
                bUsed = false;
            }
            break;
            
        case menuEvent :

            switch ( e->data.menu.itemID )
            {
                case ID_MnuClearFocused :
                    ClearTikToks( TT_CLR_FOCUSED, false );
                    break;
                    
                case ID_MnuClearStopped :
                    ClearTikToks( TT_CLR_STOPPED, false );
                    break;
                    
                case ID_MnuClearAll :
                    ClearTikToks( TT_CLR_ALL, false );
                    break;

                case ID_MnuZeroFocused :
                    ClearTikToks( TT_CLR_FOCUSED, true );
                    break;

                case ID_MnuZeroStopped :
                    ClearTikToks( TT_CLR_STOPPED, true );
                    break;

                case ID_MnuZeroAll :
                    ClearTikToks( TT_CLR_ALL, true );
                    break;

                case ID_MnuAlarm1Min :
                    StuffTikTok( "-1:" );
                    break;

                case ID_MnuAlarm2Min :
                    StuffTikTok( "-2:" );
                    break;

                case ID_MnuAlarm3Min :
                    StuffTikTok( "-3:" );
                    break;
                    
                case ID_MnuAlarm4Min :
                    StuffTikTok( "-4:" );
                    break;
                    
                case ID_MnuAlarm5Min :
                    StuffTikTok( "-5:" );
                    break;

                case ID_MnuAlarm30Min :
                    StuffTikTok( "-30:" );
                    break;

                case ID_MnuAlarm1Hr :
                    StuffTikTok( "-1::" );
                    break;

                case ID_MnuAlarm2Hr :
                    StuffTikTok( "-2::" );
                    break;

                case ID_MnuAlarm3Hr :
                    StuffTikTok( "-3::" );
                    break;
                    
                case ID_MnuAlarm4Hr :
                    StuffTikTok( "-4::" );
                    break;

                case ID_MnuAlarm5Hr :
                    StuffTikTok( "-5::" );
                    break;

                case ID_MnuAlarm1Day :
                    StuffTikTok( "-1." );
                    break;

                case ID_MnuAlarm2Day :
                    StuffTikTok( "-2." );
                    break;

                case ID_MnuEditUndo :
                    if ( TxtFieldHasFocus() )
                    {
                        FldUndo( Id2Ptr( GetFocusID() ) );
                    }
                    break;
                    
                case ID_MnuEditCut :
                    if ( TxtFieldHasFocus() )
                    {
                        FldCut( Id2Ptr( GetFocusID() ) );
                    }
                    break;

                case ID_MnuEditCopy :
                    if ( TxtFieldHasFocus() )
                    {
                        FldCopy( Id2Ptr( GetFocusID() ) );
                    }
                    break;

                case ID_MnuEditPaste :
                    if ( TxtFieldHasFocus() )
                    {
                        FldPaste( Id2Ptr( GetFocusID() ) );
                    }
                    break;

                case ID_MnuEditSelectAll :
                    if ( TxtFieldHasFocus() )
                    {
                        FldSetSelection( Id2Ptr( GetFocusID() ), 0, FldGetTextLength( Id2Ptr( GetFocusID() ) ) );
                    }
                    break;

                case ID_MnuEditKeyboard :
                    SysKeyboardDialogV10();
                    break;

                case ID_MnuOptionsPrefs :
                    FrmInitForm( ID_FrmOptions );
                    FrmPopupForm( ID_FrmOptions );
                    break;

                default :
                    bUsed = false;
                    break;
            }
            break;
            
        case keyDownEvent :
            
            switch ( e->data.keyDown.chr )
            {
                case pageDownChr :
                case pageUpChr :
                    if ( TxtFieldHasFocus() )
                    {
                        int iTikTok = FocusedTikTok();
                        
                        CtlSetValue( Id2Ptr( ID_ChkFirst + iTikTok ), !CtlGetValue( Id2Ptr( ID_ChkFirst + iTikTok ) ) );
                        ToggleTikTok( iTikTok );
                        RefreshTikTok();
                    }
                    break;
                    
                default :
                    bUsed = false;
                    break;
            }
            break;

        case winEnterEvent :
            
            bCanUpdate = ( e->data.winExit.exitWindow != (WinHandle) FrmGetFormPtr( ID_FrmTikTok ) );
            break;
            
        case nilEvent :
            
            if ( TimGetSeconds() > ulLast && bCanUpdate )
            {
                ShowTime();
                RefreshTikTok();
                ulLast = TimGetSeconds();
            }
            break;
            
        default :
            bUsed = false;
            break;
    }
    
    return( bUsed );
}

/*
 */

static Boolean PrefsFormHandleEvent( EventPtr e )
{
    Boolean bUsed = true;

    switch ( e->eType )
    {
        case frmOpenEvent :
            FrmDrawForm( FrmGetFormPtr( e->data.frmOpen.formID ) );
            CtlSetValue( Id2Ptr( ID_ChkVisAlm ),   ttPrefs.bVisAlm   );
            CtlSetValue( Id2Ptr( ID_ChkSilly ),    ttPrefs.bQuestion );
            CtlSetValue( Id2Ptr( ID_ChkShowTime ), ttPrefs.bShowTime );
            CtlSetValue( Id2Ptr( ID_ChkMemory ),   ttPrefs.bMemory   );
            CtlSetValue( Id2Ptr( ID_ChkAlmStp ),   ttPrefs.bAlmStop  );
            break;

        case ctlSelectEvent :
            switch ( e->data.ctlSelect.controlID )
            {
                case ID_ChkVisAlm :
                    ttPrefs.bVisAlm = !ttPrefs.bVisAlm;
                    break;

                case ID_ChkSilly :
                    ttPrefs.bQuestion = !ttPrefs.bQuestion;
                    break;

                case ID_ChkShowTime :
                    ttPrefs.bShowTime = !ttPrefs.bShowTime;
                    break;

                case ID_ChkMemory :
                    ttPrefs.bMemory = !ttPrefs.bMemory;
                    break;
            
                case ID_ChkAlmStp :
                    ttPrefs.bAlmStop = !ttPrefs.bAlmStop;
                    break;

                case ID_BtnOk :
                    FrmReturnToForm( ID_FrmTikTok );
                    SaveTikToks();
                    break;

                default :
                    bUsed = false;
                    break;
            }
            break;
        
        case frmCloseEvent :
            FrmEraseForm( FrmGetFormPtr( e->data.frmOpen.formID ) );
            FrmDeleteForm( FrmGetFormPtr( e->data.frmOpen.formID ) );
            break;
        
        default :
            bUsed = false;
            break;
    }
    
    return( bUsed );
}

/*
 */

static void LoadTikToks( void )
{
    Boolean bNew = !PrefGetAppPreferencesV10( TT_APP_ID, 1, (VoidPtr) &ttPrefs, sizeof( ttPrefs ) );
    int     i;

    if ( bNew )
    {
        ttPrefs.bVisAlm   = true;
        ttPrefs.iFocused  = -1;
        ttPrefs.bQuestion = true;
        ttPrefs.bShowTime = true;
        ttPrefs.bMemory   = false;
    }

    for ( i = 0; i < TT_CLOCKS; i++ )
    {
        if ( bNew )
        {
            ttPrefs.ttClocks[ i ].bOn      = false;
            ttPrefs.ttClocks[ i ].bEmpty   = true;
            ttPrefs.ttClocks[ i ].bSticky  = false;
            ttPrefs.ttClocks[ i ].lElapsed = 0L;
            ttPrefs.ttClocks[ i ].lInitial = 0L;
            ttPrefs.ttClocks[ i ].ulStart  = 0L;
        }

        ttControls[ i ].hndTime = MemHandleNew( 32 );
        ttControls[ i ].hndDesc = MemHandleNew( 32 );
    }
}

/*
 */

static void SyncTikToks( void )
{
    CharPtr sDesc;
    int     i;

    for ( i = 0; i < TT_CLOCKS; i++ )
    {
        ttControls[ i ].ptrTime = Id2Ptr( ID_SecFirst + i );
        ttControls[ i ].ptrDesc = Id2Ptr( ID_TxtFirst + i );
        
        sDesc = MemHandleLock( ttControls[ i ].hndDesc );
        StrCopy( sDesc, ttPrefs.ttClocks[ i ].sDesc );
        MemHandleUnlock( ttControls[ i ].hndDesc );
        
        FldSetTextHandle( ttControls[ i ].ptrDesc, (Handle) ttControls[ i ].hndDesc );

        FldDrawField( ttControls[ i ].ptrDesc );
        
        CtlSetValue( Id2Ptr( ID_ChkFirst + i ), ttPrefs.ttClocks[ i ].bOn );
        CtlSetValue( Id2Ptr( ID_StkFirst + i ), ttPrefs.ttClocks[ i ].bSticky );

        SetTimeFldEditable( i, !ttPrefs.ttClocks[ i ].bOn );

        ttControls[ i ].bDirty = true;
    }
}

/*
 */

static void ToggleTikTok( int iTikTok )
{
    if ( ttPrefs.ttClocks[ iTikTok ].bOn )
    {
        StopTikTok( iTikTok );
    }
    else
    {
        StartTikTok( iTikTok );
    }
}

/*
 */

static void StopTikTok( int iTikTok )
{
    ULong ulNow = TimGetSeconds();

    ttPrefs.ttClocks[ iTikTok ].lElapsed += ( ulNow - ttPrefs.ttClocks[ iTikTok ].ulStart );
    ttPrefs.ttClocks[ iTikTok ].bOn       = false;
    ttPrefs.ttClocks[ iTikTok ].bEmpty    = false;
    ttControls[ iTikTok ].bDirty          = true;

    SetTimeFldEditable( iTikTok, true );

    SaveTikToks();
    SetAlarm();
}

/*
 */

static void StartTikTok( int iTikTok )
{
    ULong   ulNow    = TimGetSeconds();
    Boolean bOk      = true;
    Long    lEntered = 0L;
    Boolean bEntered = IsTimeFieldDirty( iTikTok );

    if ( bEntered )
    {
        lEntered = ParseTimeField( iTikTok, (Boolean *) &bOk );
    }

    if ( bOk )
    {
        ttPrefs.ttClocks[ iTikTok ].bOn     = true;
        ttPrefs.ttClocks[ iTikTok ].ulStart = ulNow;
        
        if ( bEntered )
        {
            ttPrefs.ttClocks[ iTikTok ].lElapsed = lEntered;
            ttPrefs.ttClocks[ iTikTok ].lInitial = lEntered;
        }
        else if ( !ttPrefs.ttClocks[ iTikTok ].bSticky )
        {
            ttPrefs.ttClocks[ iTikTok ].lElapsed = ( ttPrefs.bMemory ? ttPrefs.ttClocks[ iTikTok ].lInitial : 0L );
        }
        
        DefaultTheDescription( iTikTok );
        
        SetTimeFldEditable( iTikTok, false );
    }
    else
    {
        CtlSetValue( Id2Ptr( ID_ChkFirst + iTikTok ), false );
    }

    SaveTikToks();
    SetAlarm();
}

/*
 */

static void ToggleSticky( int iTikTok )
{
    ttPrefs.ttClocks[ iTikTok ].bSticky = !ttPrefs.ttClocks[ iTikTok ].bSticky;
}

/*
 */

static void RefreshTikTok( void )
{
    CharPtr sTime;
    int     i;
    ULong   ulNow = TimGetSeconds();

    for ( i = 0; i < TT_CLOCKS; i++ )
    {
        if ( ttPrefs.ttClocks[ i ].bOn )
        {
            Long lTotal = ttPrefs.ttClocks[ i ].lElapsed;
            
            lTotal += ( ulNow - ttPrefs.ttClocks[ i ].ulStart );
            
            if ( ttPrefs.bAlmStop && ( ttPrefs.ttClocks[ i ].lInitial < 0L && lTotal >= 0L ) )
            {
                lTotal                         = 0L;
                ttPrefs.ttClocks[ i ].lElapsed = ( ttPrefs.bMemory ? ttPrefs.ttClocks[ i ].lInitial : 0L );
                ttPrefs.ttClocks[ i ].lInitial = ( ttPrefs.bMemory ? ttPrefs.ttClocks[ i ].lInitial : 0L );
                ttPrefs.ttClocks[ i ].bOn      = false;
                ttPrefs.ttClocks[ i ].bEmpty   = false;
                ttControls[ i ].bDirty         = true;

                SetTimeFldEditable( i, true );

                SaveTikToks();
                SyncTikToks();
            }

            sTime = (CharPtr) MemHandleLock( ttControls[ i ].hndTime );

            FormatTimer( sTime, lTotal );

            MemHandleUnlock( ttControls[ i ].hndTime );

            FldSetTextHandle( ttControls[ i ].ptrTime, (Handle) ttControls[ i ].hndTime );
            
            FldDrawField( ttControls[ i ].ptrTime );
        }
        else if ( ttControls[ i ].bDirty )
        {
            sTime = (CharPtr) MemHandleLock( ttControls[ i ].hndTime );

            if ( ttPrefs.ttClocks[ i ].bEmpty )
            {
                StrCopy( sTime, "" );
            }
            else
            {
                FormatTimer( sTime, ttPrefs.ttClocks[ i ].lElapsed );
            }
            
            MemHandleUnlock( ttControls[ i ].hndTime );

            FldSetTextHandle( ttControls[ i ].ptrTime, (Handle) ttControls[ i ].hndTime );
            
            FldDrawField( ttControls[ i ].ptrTime );

            ttControls[ i ].bDirty = false;
        }
    }
}

/*
 */

static void FormatTimer( char *sTimer, Long lElapsed )
{
    Boolean bNeg = ( lElapsed < 0 );
    UInt    uiDays;
    UInt    uiHours;
    UInt    uiMins;
    UInt    uiSecs;

    StrCopy( sTimer, ( bNeg ? "-" : "" ) );

    if ( bNeg )
    {
        lElapsed = -lElapsed;
    }
    
    uiDays   = (UInt) ( lElapsed / 86400L );
    lElapsed = (Long) ( lElapsed % 86400L );
    uiHours  = (UInt) ( lElapsed / 3600L  );
    lElapsed = (Long) ( lElapsed % 3600L  );
    uiMins   = (UInt) ( lElapsed / 60L    );
    uiSecs   = (UInt) ( lElapsed % 60L    );

    /* There has to be an easier way of doing this. Does the Pilot
       have an sprintf()? */

    ICat( uiDays, sTimer );
    StrCat( sTimer, "." );
    ZeroPadICat( uiHours, sTimer );
    StrCat( sTimer, ":" );
    ZeroPadICat( uiMins, sTimer );
    StrCat( sTimer, ":" );
    ZeroPadICat( uiSecs, sTimer );
}

/*
 */

void ICat( UInt ui, char *s )
{
    Char sBuff[ 10 ];

    StrIToA( sBuff, ui );
    StrCat( s, sBuff );
}

/*
 */

void ZeroPadICat( UInt ui, char *s )
{
    if ( ui < 10 )
    {
        StrCat( s, "0" );
    }

    ICat( ui, s );
}

/*
 */

void SaveTikToks( void )
{
    CharPtr ptrDesc;
    Word    wFocused = FrmGetFocus( FrmGetActiveForm() );
    int     iFocused = -1;
    int     i;

    if ( wFocused != -1 )
    {
        iFocused = FrmGetObjectId( FrmGetActiveForm(), wFocused );

        if ( iFocused >= ID_TxtFirst && iFocused <= ( ID_TxtFirst + TT_CLOCKS ) )
        {
            iFocused -= ID_TxtFirst;
        }
        else if ( iFocused >= ID_SecFirst && iFocused <= ( ID_SecFirst + TT_CLOCKS ) )
        {
            iFocused -= ID_SecFirst;
        }
        else
        {
            iFocused = -1;
        }
    }

    for ( i = 0; i < TT_CLOCKS; i++ )
    {
        ptrDesc = MemHandleLock( ttControls[ i ].hndDesc );
        StrCopy( ttPrefs.ttClocks[ i ].sDesc, ptrDesc );
        MemHandleUnlock( ttControls[ i ].hndDesc );
    }

    ttPrefs.iFocused = iFocused;
    
    PrefSetAppPreferencesV10( TT_APP_ID, 1, (VoidPtr) &ttPrefs, sizeof( ttPrefs ) );
}

/*
 */

void ClearTikToks( t_TikTokClear ttClrType, Boolean bSetToZero )
{
    int     iFirst = 0;
    int     iLast  = TT_CLOCKS;
    Boolean bAll   = false;
    int     i;

    switch ( ttClrType )
    {
        case TT_CLR_FOCUSED :
            if ( TxtFieldHasFocus() )
            {
                iFirst = FocusedTikTok();
                iLast  = iFirst + 1;
                bAll   = true;
            }
            break;

        case TT_CLR_STOPPED :
            bAll = false;
            break;

        case TT_CLR_ALL :
            if ( ttPrefs.bQuestion ? FrmAlert( bSetToZero ? ID_AltZeroAll : ID_AltClearAll ) != 0 : 0 )
            {
                iFirst = iLast = 0;
            }
            else
            {
                bAll = true;
            }
            break;
    }
    
    for ( i = iFirst; i < iLast; i++ )
    {
        if ( ( !ttPrefs.ttClocks[ i ].bOn && !ttPrefs.ttClocks[ i ].bSticky ) || bAll )
        {
            CharPtr sFld;

            if ( !bSetToZero )
            {
                ttPrefs.ttClocks[ i ].bOn      = false;
                ttPrefs.ttClocks[ i ].bEmpty   = true;
                ttPrefs.ttClocks[ i ].bSticky  = false;
            }
            else
            {
                ttPrefs.ttClocks[ i ].ulStart = TimGetSeconds();
            }

            ttPrefs.ttClocks[ i ].lElapsed = 0L;
            ttPrefs.ttClocks[ i ].lInitial = 0L;
            ttControls[ i ].bDirty         = true;

            if ( !bSetToZero )
            {
                sFld = (CharPtr) MemHandleLock( ttControls[ i ].hndDesc );
                StrCopy( sFld, "" );
                MemHandleUnlock( ttControls[ i ].hndDesc );
                FldSetTextHandle( ttControls[ i ].ptrDesc, (Handle) ttControls[ i ].hndDesc );
                FldDrawField( ttControls[ i ].ptrDesc );
            }

            sFld = (CharPtr) MemHandleLock( ttControls[ i ].hndTime );
            StrCopy( sFld, bSetToZero ? "0.00:00:00" : "" );
            MemHandleUnlock( ttControls[ i ].hndTime );
            FldSetTextHandle( ttControls[ i ].ptrTime, (Handle) ttControls[ i ].hndTime );
            FldDrawField( ttControls[ i ].ptrTime );

            if ( !bSetToZero )
            {
                CtlSetValue( Id2Ptr( ID_ChkFirst + i ), false );
                CtlSetValue( Id2Ptr( ID_StkFirst + i ), false );
            }

            if( !ttPrefs.ttClocks[ i ].bOn )
            {
                SetTimeFldEditable( i, true );
            }
        }
    }

    SaveTikToks();
    SetAlarm();
}

/*
 */

static VoidPtr Id2Ptr( Word wID )
{
    return( FrmGetObjectPtr( FrmGetActiveForm(), FrmGetObjectIndex( FrmGetActiveForm(), wID ) ) );
}

/*
 */

static void DefaultTheDescription( int iTikTok )
{
    CharPtr sDesc    = (CharPtr) MemHandleLock( ttControls[ iTikTok ].hndDesc );
    Boolean bHasText = false;

    while ( *sDesc && !bHasText )
    {
        bHasText = *sDesc++ != ' ';
    }

    if ( !bHasText )
    {
        CharPtr      ptrEOS;
        DateTimeType dtNow;

        TimSecondsToDateTime( TimGetSeconds(), &dtNow );

        DateToAscii( dtNow.month, dtNow.day, dtNow.year, sysPrefs.dateFormat, sDesc );

        StrCat( sDesc, " " );

        ptrEOS = sDesc + StrLen( sDesc );

        TimeToAscii( dtNow.hour, dtNow.minute, sysPrefs.timeFormat, ptrEOS );
    }

    MemHandleUnlock( ttControls[ iTikTok ].hndDesc );

    if ( !bHasText )
    {
        FldSetTextHandle( ttControls[ iTikTok ].ptrDesc, (Handle) ttControls[ iTikTok ].hndDesc );
        FldDrawField( ttControls[ iTikTok ].ptrDesc );
    }
}

/*
 */

static void DecorateForm( void )
{
    WinDrawLine(  6, 20, 25, 20 );
    WinDrawLine(  6, 20,  6, 34 );

    WinDrawLine(  6, 37,  3, 34 );
    WinDrawLine(  6, 37,  9, 34 );
    WinDrawLine(  3, 34,  9, 34 );

    WinDrawLine( 18, 30, 18, 34 );
    WinDrawLine( 18, 30, 25, 30 );

    WinDrawLine( 18, 37, 15, 34 );
    WinDrawLine( 18, 37, 21, 34 );
    WinDrawLine( 15, 34, 21, 34 );
}

/*
 */

static Boolean TxtFieldHasFocus( void )
{
    Word    wFocused  = FrmGetFocus( FrmGetActiveForm() );
    Boolean bIsTxtFld = 0;

    if ( wFocused != -1 )
    {
        wFocused  = FrmGetObjectId( FrmGetActiveForm(), wFocused );
        bIsTxtFld = ( ( wFocused >= ID_TxtFirst
                        && wFocused <= ( ID_TxtFirst + TT_CLOCKS ) ) ||
                      ( wFocused >= ID_SecFirst &&
                        wFocused < ( ID_SecFirst + TT_CLOCKS ) ) );
    }

    if ( !bIsTxtFld )
    {
        SndPlaySystemSound( sndError );
        FrmAlert( ID_AltNoSelect );
    }

    return( bIsTxtFld );
}

/*
 */

static int FocusedTikTok( void )
{
    int  iTikTok  = 0;
    Word wFocused = GetFocusID();

    if ( wFocused >= ID_TxtFirst && wFocused <= ( ID_TxtFirst + TT_CLOCKS ) )
    {
        iTikTok = wFocused - ID_TxtFirst;
    }
    else if ( wFocused >= ID_SecFirst && wFocused <= ( ID_SecFirst + TT_CLOCKS ) )
    {
        iTikTok = wFocused - ID_SecFirst;
    }

    return( iTikTok );
}

/*
 */

static Word GetFocusID( void )
{
    Word wFocused = FrmGetFocus( FrmGetActiveForm() );

    if ( wFocused != -1 )
    {
        wFocused = FrmGetObjectId( FrmGetActiveForm(), wFocused );
    }
    
    return( wFocused );
}

/*
 */

static Boolean IsTimeFieldDirty( int iTikTok )
{
    return( FldDirty( Id2Ptr( ID_SecFirst + iTikTok ) ) );
}

/*
 */

static void SetTimeFldEditable( int iTikTok, Boolean bEdit )
{
    FieldPtr      ptrTime = Id2Ptr( ID_SecFirst + iTikTok );
    FieldAttrType attr;

    FldGetAttributes( ptrTime, &attr );
    attr.editable = bEdit;
    FldSetAttributes( ptrTime, &attr );
    FldReleaseFocus( ptrTime );
}

/*
 */

static Long ParseTimeField( int iTikTok, Boolean *pbOk )
{
    Long    lTime      = 0L;
    Long    lCurrent   = 0L;
    Long    lDays      = 0L;
    int     iColons    = 0;
    CharPtr pTime      = FldGetTextPtr( Id2Ptr( ID_SecFirst + iTikTok ) );
    Boolean bNeg       = false;
    Boolean bErr       = false;
    Boolean bHitStart  = false;
    Boolean bHitDigits = false;
    Boolean bGotDays   = false;

    if ( pTime )
    {
        /* Skip any whitespace we find at the start. */
        while ( *pTime && *pTime == ' ' )
        {
            ++pTime;
        }
        
        for ( ; *pTime && !bErr; pTime++ )
        {
            switch ( *pTime )
            {
                case '-' :
                    if ( !bHitStart )
                    {
                        bNeg      = true;
                        bHitStart = true;
                    }
                    else
                    {
                        bErr = true;
                    }
                    break;

                case '+' :
                    if ( !bHitStart )
                    {
                        bNeg      = false;
                        bHitStart = true;
                    }
                    else
                    {
                        bErr = true;
                    }
                    break;

                case '.' :
                    if ( bHitStart && bHitDigits && !bGotDays )
                    {
                        lDays    = lCurrent;
                        lCurrent = 0L;
                        bGotDays = true;
                    }
                    else
                    {
                        bErr = true;
                    }
                    break;

                case ':' :
                    if ( bHitStart && bHitDigits && iColons < 2 )
                    {
                        lTime    = ( lTime + lCurrent ) * 60L;
                        lCurrent = 0L;
                        ++iColons;
                    }
                    else
                    {
                        bErr = true;
                    }
                    break;

                default :
                    if ( *pTime >= '0' && *pTime <= '9' )
                    {
                        bHitDigits = true;
                        bHitStart  = true;
                        lCurrent   = ( lCurrent * 10L ) + ( *pTime - '0' );
                    }
                    else
                    {
                        bErr = true;
                    }
            }
        }

        lTime += lCurrent;
        
        if ( lDays )
        {
            while ( iColons < 2 )
            {
                lTime *= 60L;
                ++iColons;
            }
            
            lTime += lDays * 86400L;
        }
    }

    *pbOk = !bErr;
    
    if ( bErr )
    {
        FrmAlert( ID_AltParseErr );
    }
    
    return( bNeg ? -lTime : lTime );
}

/*
 */

static void StuffTikTok( const char *pszTime )
{
    if ( TxtFieldHasFocus() )
    {
        CharPtr sTime;
        int     iTikTok = FocusedTikTok();

        StopTikTok( iTikTok );

        sTime = (CharPtr) MemHandleLock( ttControls[ iTikTok ].hndTime );
        StrCopy( sTime, pszTime );
        MemHandleUnlock( ttControls[ iTikTok ].hndTime );

        FldSetDirty( ttControls[ iTikTok ].ptrTime, true );
        CtlSetValue( Id2Ptr( ID_ChkFirst + iTikTok ), true );
                        
        StartTikTok( iTikTok );
    }
}

/*
 */

static void PutWordToFld( Word wID, Word w, Word wMaxSize, Boolean bZeroPad )
{
    FieldPtr ptrFld = Id2Ptr( wID );
    VoidHand hndFld;
    CharPtr  pszTxt;

    if ( !( hndFld = (VoidHand) FldGetTextHandle( ptrFld ) ) )
    {
        hndFld = MemHandleNew( sizeof( Char ) * wMaxSize );
    }
    pszTxt = MemHandleLock( hndFld );

    *pszTxt = 0;

    if ( bZeroPad )
    {
        ZeroPadICat( w, pszTxt );
    }
    else
    {
        ICat( w, pszTxt );
    }

    MemHandleUnlock( hndFld );
    FldSetTextHandle( ptrFld, (Handle) hndFld );
    FldDrawField( ptrFld );
}

/*
 */

static void SetFieldText( Word wID, const char *pszText, Word wMaxSize )
{
    FieldPtr ptrFld = Id2Ptr( wID );
    VoidHand hndFld;
    CharPtr  pszBuff;

    if ( !( hndFld = (VoidHand) FldGetTextHandle( ptrFld ) ) )
    {
        hndFld = MemHandleNew( sizeof( Char ) * wMaxSize );
    }

    pszBuff = MemHandleLock( hndFld );

    StrCopy( pszBuff, pszText );

    MemHandleUnlock( hndFld );
    FldSetTextHandle( ptrFld, (Handle) hndFld );
    FldDrawField( ptrFld );
}

/*
 */

static void ShowTime( void )
{
    if ( ttPrefs.bShowTime )
    {
        DateTimeType dtNow;
        Boolean      bAm;

        TimSecondsToDateTime( TimGetSeconds(), &dtNow );

        if ( !Use24HourFormat( sysPrefs.timeFormat ) )
        {
            if ( !( bAm = !( dtNow.hour > 12 ) ) )
            {
                dtNow.hour -= 12;
            }
        }

        PutWordToFld( ID_ClkHour, dtNow.hour,   3, false );
        PutWordToFld( ID_ClkMin,  dtNow.minute, 3, true  );
        PutWordToFld( ID_ClkSec,  dtNow.second, 3, true  );

        if ( Use24HourFormat( sysPrefs.timeFormat ) )
        {
            SetFieldText( ID_ClkAmPm, "", 3 );
        }
        else
        {
            SetFieldText( ID_ClkAmPm, bAm ? "am" : "pm", 3 );
        }
    }
    else
    {
        SetFieldText( ID_ClkHour, "", 3 );
        SetFieldText( ID_ClkMin,  "", 3 );
        SetFieldText( ID_ClkSec,  "", 3 );
        SetFieldText( ID_ClkAmPm, "", 3 );
    }
}

/*
 */

static ULong CalcNextAlarm( int *piTikTok )
{
    t_TikTokPrefs ttLocalPrefs;
    ULong         ulNext     = 0L;

    *piTikTok = -1;
    
    if ( PrefGetAppPreferencesV10( TT_APP_ID, 1, (VoidPtr) &ttLocalPrefs, sizeof( ttLocalPrefs ) ) )
    {
        ULong ulNow = TimGetSeconds();
        ULong ulExpires;
        int   i;

        for ( i = 0; i < TT_CLOCKS; i++ )
        {
            if ( ttLocalPrefs.ttClocks[ i ].bOn )
            {
                ulExpires = ttLocalPrefs.ttClocks[ i ].ulStart - ttLocalPrefs.ttClocks[ i ].lElapsed;

                if ( ulExpires > ulNow )
                {
                    if ( ulNext == 0 || ulExpires < ulNext )
                    {
                        *piTikTok = i;
                        ulNext    = ulExpires;
                    }
                }
            }
        }
    }

    return( ulNext );
}

/*
 */

static void SetAlarm( void )
{
    UInt              uiCardNo;
    LocalID           dbID;
    DmSearchStateType dmsDummy;

    if ( !DmGetNextDatabaseByTypeCreator( true, &dmsDummy, 'appl', TT_APP_ID, true, &uiCardNo, &dbID ) )
    {
        int   iTikTok;
        ULong ulAlarm = CalcNextAlarm( &iTikTok );
        
        AlmSetAlarm( uiCardNo, dbID, iTikTok, ulAlarm, 0 );
    }
}

/*
 */

static void RaiseAlarm( int iTikTok )
{
    VoidHand      hndPrefs;
    t_TikTokPrefs *pPrefs;
    Boolean       bVisual  = true;
    Boolean       bNoPrefs = true;

    /* When called as an alarm you can't use globals and it seeams you can't
       have large local structures either. So, in this case we'll allocate
       some memory and load the preferences into there instead. */
    
    hndPrefs = MemHandleNew( sizeof( t_TikTokPrefs ) );
    pPrefs   = (t_TikTokPrefs *) MemHandleLock( hndPrefs );
    
    if ( PrefGetAppPreferencesV10( TT_APP_ID, 1, (VoidPtr) pPrefs, sizeof( t_TikTokPrefs ) ) )
    {
        bVisual  = pPrefs->bVisAlm;
        bNoPrefs = false;
    }
    
    SndPlaySystemSound( sndAlarm );

    if ( bVisual )
    {
        VoidHand hndNote  = MemHandleNew( 50 );
        char     *pszNote = (char *) MemHandleLock( hndNote );

        if ( bNoPrefs )
        {
            StrCopy( pszNote, "(can't get details)" );
        }
        else if ( pPrefs->ttClocks[ iTikTok ].sDesc && pPrefs->ttClocks[ iTikTok ].sDesc[ 0 ] )
        {
            StrCopy( pszNote, pPrefs->ttClocks[ iTikTok ].sDesc );
        }
        else
        {
            StrCopy( pszNote, "(untitled)" );
        }
        
        FrmCustomAlert( ID_AltAlarm, pszNote, GCCNULL, GCCNULL );

        MemHandleUnlock( hndNote );
        MemHandleFree( hndNote );
    }

    MemHandleUnlock( hndPrefs );
    MemHandleFree( hndPrefs );
}
