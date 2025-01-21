/**********************************************************************************************************************
 * 
 * Epson TM Printer Driver (ESC/POS) for Linux
 * 
 * Copyright (C) Seiko Epson Corporation 2019.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 *********************************************************************************************************************/
#include <cups/ppd.h>
#include <cups/raster.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h> // LONG_MAX

/*---------------------------------------------------------------------------------------------------------------------
 * Result code
 *-------------------------------------------------------------------------------------------------------------------*/
#define EPTMD_SUCCESS (0)	// Processing succeeded.
#define EPTMD_FAILED (-1)	// Processing failed.
#define EPTMD_CANCEL (-2)	// Processing canceled.

/*---------------------------------------------------------------------------------------------------------------------
 * command declaration
 *-------------------------------------------------------------------------------------------------------------------*/
#define ESC (0x1b)
#define GS  (0x1d)
#define FF  (0x0c)

/*---------------------------------------------------------------------------------------------------------------------
 * enum declaration
 *-------------------------------------------------------------------------------------------------------------------*/
typedef enum {
	TmPaperReductionOff = 0,
	TmPaperReductionTop,
	TmPaperReductionBottom,
	TmPaperReductionBoth,
} EPTME_BLANK_SKIP_TYPE;									// Paper Reduction

typedef enum {
	TmBuzzerNotUsed = 0,
	TmBuzzerInternal,
	TmBuzzerExternal,
} EPTME_BUZZER;												// Buzzer Type

typedef enum {
	TmDrawerNotUsed = 0,
	TmDrawer1,
	TmDrawer2,
} EPTME_DRAWER;												// Drawer No

/*---------------------------------------------------------------------------------------------------------------------
 * Stracture prototype declaration
 *-------------------------------------------------------------------------------------------------------------------*/
typedef struct {
	char*						p_printerName;				// The name of the destination printer.
	
	unsigned					h_motionUnit;				// Horizontal motion units.
	unsigned					v_motionUnit;				// Vertical motion units.
	
	EPTME_BLANK_SKIP_TYPE		paperReduction;				// Paper reduction settings.
	EPTME_BUZZER				buzzerControl;				// Buzzer control settings.
	EPTME_DRAWER				drawerControl;				// Drawer control settings.
	
	unsigned					maxBandLines;				// Maximum band length.
} EPTMS_CONFIG_T;											// Configuration parameters

typedef struct {
	cups_raster_t*				p_raster;					
	cups_page_header_t			pageHeader;					
	
	unsigned char*				p_pageBuffer;				
} EPTMS_JOB_INFO_T;											// Job Information parameters

/*---------------------------------------------------------------------------------------------------------------------
 * Global variable declaration
 *-------------------------------------------------------------------------------------------------------------------*/
char g_TmCanceled;

/*---------------------------------------------------------------------------------------------------------------------
 * Static function prototype declaration
 *-------------------------------------------------------------------------------------------------------------------*/
static void fprintf_DebugLog(EPTMS_CONFIG_T*);
static int  Init(int, char *[], EPTMS_CONFIG_T*, EPTMS_JOB_INFO_T*, int*);
static int  InitSignal(void);
static void SignalCallback(int);
static int  GetParameters(char*[], EPTMS_CONFIG_T*);
static int  GetModelSpecificFromPPD(ppd_file_t*, EPTMS_CONFIG_T*);
static int  GetPaperReductionFromPPD(ppd_file_t*, EPTMS_CONFIG_T*);
static int  GetBuzzerAndDrawerFromPPD(ppd_file_t*, EPTMS_CONFIG_T*);
static void Exit(EPTMS_JOB_INFO_T*, int*);

static int  DoJob(EPTMS_CONFIG_T*, EPTMS_JOB_INFO_T*);
static int  StartJob(EPTMS_CONFIG_T*, EPTMS_JOB_INFO_T*);
static int  OpenDrawer(EPTMS_CONFIG_T*);
static int  SoundBuzzer(EPTMS_CONFIG_T*);
static int  EndJob(EPTMS_CONFIG_T*, EPTMS_JOB_INFO_T*);

static int  DoPage(EPTMS_CONFIG_T*, EPTMS_JOB_INFO_T*);
static int  StartPage(EPTMS_CONFIG_T*);
static int  EndPage(EPTMS_CONFIG_T*, cups_page_header_t*);
static int  ReadRaster(cups_page_header_t*, cups_raster_t*, unsigned char*);
static void TransferRaster(unsigned char*, unsigned char*, cups_page_header_t*, unsigned);
static int  WriteRaster(EPTMS_CONFIG_T*, cups_page_header_t*, unsigned char*);
static void AvoidDisturbingData(cups_page_header_t*, unsigned char*, unsigned, unsigned);
static unsigned FindBlackRasterLineTop(cups_page_header_t*, unsigned char*);
static unsigned FindBlackRasterLineEnd(cups_page_header_t*, unsigned char*);
static int  WriteBand(EPTMS_CONFIG_T* p_config, cups_page_header_t*, unsigned char*, unsigned);

static int  WriteUserFile(char*, char*);
static int  ReadUserFile(int, void*, int);
static int  FeedPaper(EPTMS_CONFIG_T*, cups_page_header_t*, unsigned);
static int  WriteData(unsigned char*, unsigned int);

/*---------------------------------------------------------------------------------------------------------------------
 * Main function for process.
 *-------------------------------------------------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
	EPTMS_JOB_INFO_T JobInfo = {0};
	EPTMS_CONFIG_T	 Config  = {0};
	int				 InputFd = -1;
	int				 result  = EPTMD_SUCCESS;
	
	// Initializes process.
	result = Init( argc, argv, &Config, &JobInfo, &InputFd );
	
	// Processing print job.
	if ( EPTMD_SUCCESS == result ) {
		result = DoJob( &Config, &JobInfo );
	}
	
	// Finalizes process.
	Exit( &JobInfo, &InputFd );
	
	// Error log output.
	if ( EPTMD_SUCCESS != result ) {
		fprintf( stderr, "ERROR: Error Code=%d\n", result );
	}
	
	// Output message for debugging.
	fprintf_DebugLog( &Config );
	
	if ( result == EPTMD_SUCCESS ) {
		return 0;  // SUCCESS
	}
	else if ( result == EPTMD_CANCEL ) {
		return -2; // CANCEL
	}
	else {
		return -1; // FAILED
	}
}

/*---------------------------------------------------------------------------------------------------------------------
 * Output message for debugging.
 *-------------------------------------------------------------------------------------------------------------------*/
static void fprintf_DebugLog(EPTMS_CONFIG_T* p_config)
{
	fprintf( stderr, "DEBUG:       p_printerName = %s\n",  p_config->p_printerName       );
	fprintf( stderr, "DEBUG:        v_motionUnit = %u\n",  p_config->v_motionUnit        );
	fprintf( stderr, "DEBUG:        h_motionUnit = %u\n",  p_config->h_motionUnit        );
	fprintf( stderr, "DEBUG:      paperReduction = %d\n",  p_config->paperReduction      );
	fprintf( stderr, "DEBUG:       buzzerControl = %d\n",  p_config->buzzerControl       );
	fprintf( stderr, "DEBUG:       drawerControl = %d\n",  p_config->drawerControl       );
	fprintf( stderr, "DEBUG:        maxBandLines = %u\n",  p_config->maxBandLines        );
}

/*---------------------------------------------------------------------------------------------------------------------
 * Initializes process.
 *-------------------------------------------------------------------------------------------------------------------*/
static int Init(int argc, char *argv[], EPTMS_CONFIG_T* p_config, EPTMS_JOB_INFO_T* p_jobInfo, int* p_InputFd)
{
	int result = EPTMD_SUCCESS;
	
	// Initializes global variables.
	g_TmCanceled = 0;
	
	// Check parameters.
	if ( (NULL == argv) || ((6 != argc) && (7 != argc)) ) {
		return 1001;
	}
	
	// Initializes signals.
	result = InitSignal();
	if ( EPTMD_SUCCESS != result ) { return result; }
	
	{ // Open a raster stream.
		if ( 6 == argc ) {
			*p_InputFd = 0;
		}
		else if ( 7 == argc ) {
			*p_InputFd = open( argv[6], O_RDONLY );
			if ( *p_InputFd < 0 ) {
				return 1002;
			}
		}
		else {}
		
		p_jobInfo->p_raster = cupsRasterOpen( *p_InputFd, CUPS_RASTER_READ );
		if ( NULL == p_jobInfo->p_raster ) {
			return 1003;
		}
	}
	
	// Get parameters.
	result = GetParameters( argv, p_config );
	if ( EPTMD_SUCCESS != result ) { return result; }
	
	// Get printer name.
	p_config->p_printerName = argv[0];
	p_config->maxBandLines  = 8;
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Initializes signals.
 *-------------------------------------------------------------------------------------------------------------------*/
static int InitSignal(void)
{
	sigset_t sigset;
	{ // 
		if ( 0 != sigemptyset( &sigset ) ) {
			return 1101;
		}
		if ( 0 != sigaddset( &sigset, SIGTERM ) ) {
			return 1102;
		}
		if ( 0 != sigprocmask( SIG_BLOCK, &sigset, NULL ) ) {
			return 1103;
		}
	}
	{ // 
		struct sigaction sigact_term;
		if ( 0 != sigaction( SIGTERM, NULL, &sigact_term ) ) {
			return 1104;
		}
		sigact_term.sa_handler = SignalCallback;
		sigact_term.sa_flags  |= SA_RESTART;
		if ( 0 != sigaction( SIGTERM, &sigact_term, NULL ) ) {
			return 1105;
		}
	}
	
	if ( 0 != sigprocmask( SIG_UNBLOCK, &sigset, NULL ) ) {
		return 1106;
	}
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Callback for signals.
 *-------------------------------------------------------------------------------------------------------------------*/
static void SignalCallback(int signal_id)
{
	(void)signal_id;	// unused parameter
	
	g_TmCanceled = 1;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Get parameters.
 *-------------------------------------------------------------------------------------------------------------------*/
static int GetParameters(char *argv[], EPTMS_CONFIG_T *p_config)
{
	ppd_file_t* p_ppd = NULL;
	{ // Load the PPD file
		p_ppd = ppdOpenFile( getenv("PPD") );
		if ( NULL == p_ppd ) { return 4001; }
		
		ppdMarkDefaults(p_ppd);
	}
	{ // Check conflict
		cups_option_t*	p_options = NULL;
		
		int num_option = cupsParseOptions( argv[5], 0, &p_options );
		if ( 0 < num_option ) {
			if ( 0 != cupsMarkOptions( p_ppd, num_option, p_options ) ) { // 1 if conflicts exist, 0 otherwise
				ppdClose( p_ppd );
				cupsFreeOptions( num_option, p_options );
				return 4002;
			}
		}
		cupsFreeOptions( num_option, p_options );
	}
	int result = EPTMD_SUCCESS;
	{ // Get parameters
		
		if ( EPTMD_SUCCESS == result ) {
			result = GetModelSpecificFromPPD( p_ppd, p_config );
		}
		if ( EPTMD_SUCCESS == result ) {
			result = GetPaperReductionFromPPD( p_ppd, p_config );
		}
		if ( EPTMD_SUCCESS == result ) {
			result = GetBuzzerAndDrawerFromPPD( p_ppd, p_config );
		}
	}
	// Unload the PPD file
	ppdClose( p_ppd );
	
	return result;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Get Model-specific settings.
 *-------------------------------------------------------------------------------------------------------------------*/
static int GetModelSpecificFromPPD(ppd_file_t *p_ppd, EPTMS_CONFIG_T *p_config)
{
	{
		char ppdKeyMotionUnit[] = "TmxMotionUnitHori";
		ppd_attr_t* p_attribute = ppdFindAttr( p_ppd, ppdKeyMotionUnit, NULL );
		if ( NULL == p_attribute ) { return 4101; }
		
		p_config->h_motionUnit = (unsigned)atol( p_attribute->value );
		
		if ( (0 == p_config->h_motionUnit) || (255 < p_config->h_motionUnit) ) { // GS P Command Specification
			return 4102;
		}
	}
	{
		char ppdKeyMotionUnit[] = "TmxMotionUnitVert";
		ppd_attr_t* p_attribute = ppdFindAttr( p_ppd, ppdKeyMotionUnit, NULL );
		if ( NULL == p_attribute ) { return 4103; }
		
		p_config->v_motionUnit = (unsigned)atol( p_attribute->value );
		
		if ( (0 == p_config->v_motionUnit) || (255 < p_config->v_motionUnit) ) { // GS P Command Specification
			return 4104;
		}
	}
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Get blank skip settings.
 *-------------------------------------------------------------------------------------------------------------------*/
static int GetPaperReductionFromPPD(ppd_file_t *p_ppd, EPTMS_CONFIG_T *p_config)
{
	char ppdKey[] = "TmxPaperReduction";
	
	ppd_choice_t* p_choice = ppdFindMarkedChoice( p_ppd, ppdKey );
	if ( NULL == p_choice ) { return 4201; }
	
	if ( 0 == strcmp( "Off", p_choice->choice ) ) {
		p_config->paperReduction = TmPaperReductionOff;
	}
	else if ( 0 == strcmp( "Top", p_choice->choice ) ) {
		p_config->paperReduction = TmPaperReductionTop;
	}
	else if ( 0 == strcmp( "Bottom", p_choice->choice ) ) {
		p_config->paperReduction = TmPaperReductionBottom;
	}
	else if ( 0 == strcmp( "Both", p_choice->choice ) ) {
		p_config->paperReduction = TmPaperReductionBoth;
	}
	else { return 4202; }
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Get buzzer and drawer setting.
 *-------------------------------------------------------------------------------------------------------------------*/
static int GetBuzzerAndDrawerFromPPD(ppd_file_t *p_ppd, EPTMS_CONFIG_T *p_config)
{
	char ppdKey[] = "TmxBuzzerAndDrawer";
	
	ppd_choice_t* p_choice = ppdFindMarkedChoice( p_ppd, ppdKey );
	if ( NULL == p_choice ) { return 4301; }
	
	if ( 0 == strcmp( "NotUsed", p_choice->choice ) ) {
		p_config->buzzerControl = TmBuzzerNotUsed;
		p_config->drawerControl = TmDrawerNotUsed;
	}
	else if ( 0 == strcmp( "InternalBuzzer", p_choice->choice ) ) {
		p_config->buzzerControl = TmBuzzerInternal;
	}
	else if ( 0 == strcmp( "ExternalBuzzer", p_choice->choice ) ) {
		p_config->buzzerControl = TmBuzzerExternal;
	}
	else if ( 0 == strcmp( "OpenDrawer1", p_choice->choice ) ) {
		p_config->drawerControl = TmDrawer1;
	}
	else if ( 0 == strcmp( "OpenDrawer2", p_choice->choice ) ) {
		p_config->drawerControl = TmDrawer2;
	}
	else { return 4302; }
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Finalizes process.
 *-------------------------------------------------------------------------------------------------------------------*/
static void Exit(EPTMS_JOB_INFO_T* p_jobInfo, int* p_InputFd)
{
	if ( NULL != p_jobInfo->p_raster ) {
		cupsRasterClose( p_jobInfo->p_raster );
		p_jobInfo->p_raster = NULL;
	}
	
	if ( 0 < *p_InputFd ) {
		close( *p_InputFd );
		*p_InputFd = -1;
	}
}

/*---------------------------------------------------------------------------------------------------------------------
 * Processing print job.
 *-------------------------------------------------------------------------------------------------------------------*/
static int DoJob(EPTMS_CONFIG_T* p_config, EPTMS_JOB_INFO_T* p_jobInfo)
{
	int result = EPTMD_SUCCESS;
	unsigned page = 0;
	
	result = StartJob( p_config, p_jobInfo );
	
	while ( EPTMD_SUCCESS == result )
	{
		if ( 0 == cupsRasterReadHeader( p_jobInfo->p_raster, &p_jobInfo->pageHeader ) ) {
			result = EPTMD_SUCCESS;
			break;
		}
		
		page++;
		fprintf( stderr, "PAGE: %u %d\n"                 , page, p_jobInfo->pageHeader.NumCopies  );
		fprintf( stderr, "DEBUG: cupsBytesPerLine = %u\n", p_jobInfo->pageHeader.cupsBytesPerLine );
		fprintf( stderr, "DEBUG: cupsBitsPerPixel = %u\n", p_jobInfo->pageHeader.cupsBitsPerPixel );
		fprintf( stderr, "DEBUG: cupsBitsPerColor = %u\n", p_jobInfo->pageHeader.cupsBitsPerColor );
		fprintf( stderr, "DEBUG:       cupsHeight = %u\n", p_jobInfo->pageHeader.cupsHeight       );
		fprintf( stderr, "DEBUG:        cupsWidth = %u\n", p_jobInfo->pageHeader.cupsWidth        );
		
		if ( 1 != p_jobInfo->pageHeader.cupsBitsPerPixel ) {
			result = 2001;
			break;
		}
		
		if ( NULL == p_jobInfo->p_pageBuffer ) { // Allocate buffer of page.
			long size = p_jobInfo->pageHeader.cupsHeight * p_jobInfo->pageHeader.cupsBytesPerLine;
			p_jobInfo->p_pageBuffer = (unsigned char *)malloc( size );
			if ( NULL == p_jobInfo->p_pageBuffer ) {
				result = 2002;
				break;
			}
			memset( p_jobInfo->p_pageBuffer, 0, size );
		}
		
		result = DoPage( p_config, p_jobInfo );
	}
	
	// Free buffer of page.
	if ( NULL != p_jobInfo->p_pageBuffer ) {
		free( p_jobInfo->p_pageBuffer );
		p_jobInfo->p_pageBuffer = NULL;
	}
	
	if ( EPTMD_SUCCESS != result ) {
		EndJob( p_config, p_jobInfo );
	}
	else {
		result = EndJob( p_config, p_jobInfo );
	}
	
	return result;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Start job.
 *-------------------------------------------------------------------------------------------------------------------*/
static int StartJob(EPTMS_CONFIG_T* p_config, EPTMS_JOB_INFO_T* p_jobInfo)
{
	int result = EPTMD_SUCCESS;
	
	if ( 0 != g_TmCanceled ) {
		return EPTMD_CANCEL;
	}
	
	{ // Write configuration commands.
		unsigned char CommandSetDevice[3+2] = { ESC, '=', 0x01, ESC, '@' };
		result = WriteData( CommandSetDevice, sizeof(CommandSetDevice) );
		if ( EPTMD_SUCCESS != result ) { return 2101; }
		
		unsigned char CommandSetPrintSheet[4] = { ESC, 'c', '0', 0x04 };
		result = WriteData( CommandSetPrintSheet, sizeof(CommandSetPrintSheet) );
		if ( EPTMD_SUCCESS != result ) { return 2102; }
		
		unsigned char CommandSetConfigSheet[4] = { ESC, 'c', '1', 0x04 };
		result = WriteData( CommandSetConfigSheet, sizeof(CommandSetConfigSheet) );
		if ( EPTMD_SUCCESS != result ) { return 2103; }
		
		unsigned char CommandSetNearendPrint[4] = { ESC, 'c', '3', 0x00 };
		result = WriteData( CommandSetNearendPrint, sizeof(CommandSetNearendPrint) );
		if ( EPTMD_SUCCESS != result ) { return 2104; }
		
		unsigned char CommandSelectTheSideOfTheSlip[7] = { GS, '(', 'G', 2, 0, 48, 4 };
		result = WriteData( CommandSelectTheSideOfTheSlip, sizeof(CommandSelectTheSideOfTheSlip) );
		if ( EPTMD_SUCCESS != result ) { return 2105; }
	}
	
	// Drawer open.
	result = OpenDrawer( p_config );
	if ( EPTMD_SUCCESS != result ) { return 2106; }
	
	// Sound buzzer.
	result = SoundBuzzer( p_config );
	if ( EPTMD_SUCCESS != result ) { return 2107; }
	
	// Send user file.
	result = WriteUserFile( p_config->p_printerName, "StartJob.prn" );
	if ( EPTMD_SUCCESS != result ) { return 2108; }
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Open Drawer.
 *-------------------------------------------------------------------------------------------------------------------*/
static int OpenDrawer(EPTMS_CONFIG_T* p_config)
{
	int result = EPTMD_SUCCESS;
	
	if ( TmDrawerNotUsed == p_config->drawerControl ) {
		return EPTMD_SUCCESS;
	}
	
	unsigned char Command[5] = { ESC, 'p', 0, 50 /* on time */, 200 /* off time */ };
	Command[2] = p_config->drawerControl - 1; // pin no
	
	result = WriteData( Command, sizeof(Command) );
	
	return result;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Sound buzzer.
 *-------------------------------------------------------------------------------------------------------------------*/
static int SoundBuzzer(EPTMS_CONFIG_T* p_config)
{
	int result = EPTMD_SUCCESS;
	
	if ( TmBuzzerNotUsed == p_config->buzzerControl ) {
		return EPTMD_SUCCESS;
	}
	else if ( TmBuzzerInternal == p_config->buzzerControl ) {					// Sound internal buzzer
		unsigned char Command[5] = { ESC, 'p', 1/* pin no */, 50/* on time */, 200/* off time */ };
		
		int n;
		for ( n = 0; n < 1 /* repeat count */; n++ ) {
			result = WriteData( Command, sizeof(Command) );
			if ( EPTMD_SUCCESS != result ) { return result; }
		}
	}
	else if ( TmBuzzerExternal == p_config->buzzerControl ) {					// Sound external buzzer
		unsigned char Command[10] = { ESC, '(', 'A', 5, 0, 97, 100, 1, 50/* on time */, 200/* off time */ };
		result = WriteData( Command, sizeof(Command) );
		if ( EPTMD_SUCCESS != result ) { return result; }
	}
	else {}
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * End job.
 *-------------------------------------------------------------------------------------------------------------------*/
static int EndJob(EPTMS_CONFIG_T* p_config, EPTMS_JOB_INFO_T* p_jobInfo)
{
	int result = EPTMD_SUCCESS;
	
	if ( 0 != g_TmCanceled ) {
		return EPTMD_CANCEL;
	}
	
	// Send user file.
	result = WriteUserFile( p_config->p_printerName, "EndJob.prn" );
	if ( EPTMD_SUCCESS != result ) { return 2201; }
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Processing print page.
 *-------------------------------------------------------------------------------------------------------------------*/
static int DoPage(EPTMS_CONFIG_T* p_config, EPTMS_JOB_INFO_T* p_jobInfo)
{
	int result;
	
	result = StartPage( p_config );
	
	if ( EPTMD_SUCCESS == result ) {
		result = ReadRaster( &p_jobInfo->pageHeader, p_jobInfo->p_raster, p_jobInfo->p_pageBuffer );
	}
	
	if ( EPTMD_SUCCESS == result ) {
		result = WriteRaster( p_config, &p_jobInfo->pageHeader, p_jobInfo->p_pageBuffer );
	}
	
	if ( EPTMD_SUCCESS == result ) {
		result = EndPage( p_config, &p_jobInfo->pageHeader );
	}
	
	return result;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Start page.
 *-------------------------------------------------------------------------------------------------------------------*/
static int StartPage(EPTMS_CONFIG_T* p_config)
{
	int result;
	
	// Feed to the print starting position for the slip
	unsigned char CommandFeedToThePrintStartingPosition[7] = { GS, '(', 'G', 2, 0, 84, 1 };
	result = WriteData( CommandFeedToThePrintStartingPosition, sizeof(CommandFeedToThePrintStartingPosition) );
	if ( EPTMD_SUCCESS != result ) { return 3101; }
	
	// Send user file.
	result = WriteUserFile( p_config->p_printerName, "StartPage.prn" );
	if ( EPTMD_SUCCESS != result ) { return 3102; }
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * End page.
 *-------------------------------------------------------------------------------------------------------------------*/
static int EndPage(EPTMS_CONFIG_T* p_config, cups_page_header_t* p_header)
{
	int result;
	
	if ( 0 != g_TmCanceled ) {
		return EPTMD_CANCEL;
	}
	
	// Send user file.
	result = WriteUserFile( p_config->p_printerName, "EndPage.prn" );
	if ( EPTMD_SUCCESS != result ) { return 3201; }
	
	// Print and eject cut sheet.
	unsigned char Command[3+1] = { ESC, 'F', 0, FF };
	result = WriteData( Command, sizeof(Command) );
	if ( EPTMD_SUCCESS != result ) { return 3202; }
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Read raster data of one page.
 *-------------------------------------------------------------------------------------------------------------------*/
static int ReadRaster(cups_page_header_t* p_header, cups_raster_t* p_raster, unsigned char* p_pageBuffer)
{
	int				result = EPTMD_SUCCESS;
	unsigned char*	p_data;
	unsigned		data_size = p_header->cupsBytesPerLine;
	
	p_data = (unsigned char*)malloc( data_size );
	if ( NULL == p_data ) {
		return 3301;
	}
	memset( p_data, 0, data_size );
	
	unsigned i;
	for ( i = 0; i < p_header->cupsHeight; i++ ) {
		if ( 0 != g_TmCanceled ) {
			result = EPTMD_CANCEL;
			break;
		}
		
		unsigned num_bytes_read = cupsRasterReadPixels( p_raster, p_data, data_size );
		if ( data_size > num_bytes_read ) {
			fprintf( stderr, "DEBUG: cupsRasterReadPixels() = %u:%u/%u\n", (i + 1), num_bytes_read, data_size );
			result = 3302;
			break;
		}
		
		TransferRaster( p_pageBuffer, p_data, p_header, i );
	}
	
	free( p_data );
	
	return result;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Transfer raster to page buffer for 1bit per pixel data.
 *-------------------------------------------------------------------------------------------------------------------*/
static void TransferRaster(unsigned char* p_pageBuffer, unsigned char *p_data, cups_page_header_t* p_header, unsigned line_no)
{
	unsigned char*	p_dest   = p_pageBuffer + p_header->cupsBytesPerLine * line_no;
	
	memcpy( p_dest, p_data, p_header->cupsBytesPerLine );
}

/*---------------------------------------------------------------------------------------------------------------------
 * Write raster data of one page.
 *-------------------------------------------------------------------------------------------------------------------*/
static int WriteRaster(EPTMS_CONFIG_T* p_config, cups_page_header_t* p_header, unsigned char* p_pageBuffer)
{
	unsigned 		line_no = 0;
	unsigned 		start_line_no = 0;	/* first raster line without top blank */
	unsigned 		last_line_no  = 0;	/* last raster line without bottom blank */
	unsigned char*	p_data = NULL;
	int				result = EPTMD_SUCCESS;
	
	// Get top margin
	start_line_no = FindBlackRasterLineTop( p_header, p_pageBuffer );
	if( p_header->cupsHeight == start_line_no ) { /* This page has not image */
		if ( TmPaperReductionOff == p_config->paperReduction ) {
			result = FeedPaper( p_config, p_header, p_header->cupsHeight );
			if ( EPTMD_SUCCESS != result ) { return 3401; }
		}
		return EPTMD_SUCCESS;
	}
	
	// Get bottom margin
	last_line_no = FindBlackRasterLineEnd( p_header, p_pageBuffer ) + 1;
	
	// Command output : top margin
	if ( !((TmPaperReductionTop == p_config->paperReduction) || (TmPaperReductionBoth == p_config->paperReduction)) ) {
		result = FeedPaper( p_config, p_header, start_line_no );
		if ( EPTMD_SUCCESS != result ) { return 3402; }
	}
	// Command output : raster data (band unit)
	for ( line_no = start_line_no; (line_no + p_config->maxBandLines) < last_line_no; line_no+=p_config->maxBandLines ) {
		p_data = p_pageBuffer + (p_header->cupsBytesPerLine * line_no);
		result = WriteBand( p_config, p_header, p_data, p_config->maxBandLines );
		if ( EPTMD_SUCCESS != result ) { return 3403; }
		
		if ( 0 != g_TmCanceled ) {
			return EPTMD_CANCEL;
		}
	}
	// Command output : raster data
	if ( line_no < last_line_no ) {
		p_data = p_pageBuffer + (p_header->cupsBytesPerLine * line_no);
		result = WriteBand( p_config, p_header, p_data, (last_line_no - line_no) );
		if ( EPTMD_SUCCESS != result ) { return 3404; }
	}
	// Command output : Bottom margin
	if ( !((TmPaperReductionBottom == p_config->paperReduction) || (TmPaperReductionBoth == p_config->paperReduction)) ) {
		result = FeedPaper( p_config, p_header, (p_header->cupsHeight - last_line_no) );
		if ( EPTMD_SUCCESS != result ) { return 3405; }
	}
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Avoid disturbing data.
 *-------------------------------------------------------------------------------------------------------------------*/
static void AvoidDisturbingData(cups_page_header_t* p_header, unsigned char* p_pageBuffer, unsigned start_line_no, unsigned last_line_no)
{
	unsigned char*	p_data = p_pageBuffer + (p_header->cupsBytesPerLine * start_line_no);
	unsigned long	data_size = (last_line_no - start_line_no) * p_header->cupsBytesPerLine;
	
	unsigned long i;
	for ( ; (i + 1) < data_size; i++ ) {
		if ( 0x10 == p_data[i] ) {
			if ( (0x04 == p_data[i+1]) || (0x05 == p_data[i+1]) || (0x14 == p_data[i+1]) ) {
				p_data[i] = 0x30;
			}
		}
		else if ( 0x1B == p_data[i] ) {
			if ( 0x3D == p_data[i+1] ) {
				p_data[i] = 0x3B;
			}
		}
		else {}
	}
}

/*---------------------------------------------------------------------------------------------------------------------
 * Find black-raster-line top.
 *-------------------------------------------------------------------------------------------------------------------*/
static unsigned FindBlackRasterLineTop(cups_page_header_t* p_header, unsigned char* p_pageBuffer)
{
	unsigned       BytesPerLine = p_header->cupsBytesPerLine;
	unsigned char* p_data = p_pageBuffer;
	
	unsigned y;
	for ( y = 0; y < p_header->cupsHeight; y++ ) {
		unsigned x;
		for ( x = 0 ; x < BytesPerLine; x++ ) {
			if ( 0x00 != p_data[x] ) {
				return y;
			}
		}
		p_data += BytesPerLine;
	}
	
	return p_header->cupsHeight;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Find black-raster-line end.
 *-------------------------------------------------------------------------------------------------------------------*/
static unsigned FindBlackRasterLineEnd(cups_page_header_t* p_header, unsigned char* p_pageBuffer)
{
	unsigned       BytesPerLine = p_header->cupsBytesPerLine;
	unsigned char* p_data = p_pageBuffer + (BytesPerLine * (p_header->cupsHeight - 1));
	
	unsigned y;
	for ( y = 0; y < p_header->cupsHeight; y++ ) {
		unsigned x;
		for ( x = 0 ; x < BytesPerLine; x++ ) {
			if ( 0x00 != p_data[x] ) {
			  return (p_header->cupsHeight - 1 - y);
			}
		}
		p_data -= BytesPerLine;
	}
	
	return 0;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Band out.
 *-------------------------------------------------------------------------------------------------------------------*/
static int WriteBand(EPTMS_CONFIG_T* p_config, cups_page_header_t* p_header, unsigned char *p_data, unsigned lines)
{
	int				result			= EPTMD_SUCCESS;
	unsigned char*	p_send_data		= NULL;
	unsigned long	send_data_size	= p_header->cupsBytesPerLine * 8/* height */;
	
//	if ( 8 < lines ) { return EPTMD_FAILED; }	// 9pin only
	
	unsigned char Command[5] = { ESC, '*', 1, 0, 0 };
	Command[3] = (unsigned char)((p_header->cupsWidth     ) & 0xff);
	Command[4] = (unsigned char)((p_header->cupsWidth >> 8) & 0xff);
	result = WriteData( Command, sizeof(Command) );
	if ( EPTMD_SUCCESS != result ) { return result; }
	
	p_send_data = (unsigned char*)malloc( send_data_size );
	if ( NULL == p_send_data ) { return EPTMD_FAILED; }
	memset( p_send_data, 0, send_data_size );
	
	{ // Make send-data
		unsigned char*	p_data0 = p_data;
		unsigned char*	p_data1 = p_data0 + p_header->cupsBytesPerLine;
		unsigned char*	p_data2 = p_data1 + p_header->cupsBytesPerLine;
		unsigned char*	p_data3 = p_data2 + p_header->cupsBytesPerLine;
		unsigned char*	p_data4 = p_data3 + p_header->cupsBytesPerLine;
		unsigned char*	p_data5 = p_data4 + p_header->cupsBytesPerLine;
		unsigned char*	p_data6 = p_data5 + p_header->cupsBytesPerLine;
		unsigned char*	p_data7 = p_data6 + p_header->cupsBytesPerLine;
		
		unsigned long x, bit, index = 0;
		for ( x = 0; x < p_header->cupsBytesPerLine; x++ ) {
			
			for ( bit = 0; bit < 8/* bit per byte */; bit++ ) {
				
				if( 1 <= lines ) { p_send_data[index] =  (((p_data0[x] >> (7 - bit)) << 7) & 0x80); }
				if( 2 <= lines ) { p_send_data[index] |= (((p_data1[x] >> (7 - bit)) << 6) & 0x40); }
				if( 3 <= lines ) { p_send_data[index] |= (((p_data2[x] >> (7 - bit)) << 5) & 0x20); }
				if( 4 <= lines ) { p_send_data[index] |= (((p_data3[x] >> (7 - bit)) << 4) & 0x10); }
				if( 5 <= lines ) { p_send_data[index] |= (((p_data4[x] >> (7 - bit)) << 3) & 0x08); }
				if( 6 <= lines ) { p_send_data[index] |= (((p_data5[x] >> (7 - bit)) << 2) & 0x04); }
				if( 7 <= lines ) { p_send_data[index] |= (((p_data6[x] >> (7 - bit)) << 1) & 0x02); }
				if( 8 <= lines ) { p_send_data[index] |= (((p_data7[x] >> (7 - bit)) << 0) & 0x01); }
				
				index++;
			}
		}
		
		// Avoid disturbing data
		AvoidDisturbingData( p_header, p_send_data, 0, 8/* height */ );
	}
	result = WriteData( p_send_data, (unsigned int)send_data_size );
	if ( NULL != p_send_data     ) { free(p_send_data); }
	if ( EPTMD_SUCCESS != result ) { return result; }
	
	result = FeedPaper( p_config, p_header, 8/* height */ );
	if ( EPTMD_SUCCESS != result ) { return result; }
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Write user-file.
 *-------------------------------------------------------------------------------------------------------------------*/
static int WriteUserFile(char *p_printerName, char *p_file_name)
{
	// Output a file if it exists in a predetermined place. : /var/lib/tmx-cups
	
	char path[512 + 1];
#ifndef EPD_TM_MAC
    snprintf( path, sizeof(path)-1, "%s/%s_%s", "/var/lib/tmx-cups", p_printerName, p_file_name );
#else
    snprintf( path, sizeof(path)-1, "%s/%s_%s", "/Library/Caches/Epson/TerminalPrinter", p_printerName, p_file_name );
#endif
	
	int fd = open( path, O_RDONLY );
	if ( 0 > fd ) {
		if ( ENOENT == errno ) { // No such file or directory
			return EPTMD_SUCCESS;
		}
		return EPTMD_FAILED;
	}
	
	while ( 1 )
	{
		char data[1024] = { 0 };
		int  size = ReadUserFile( fd, data, sizeof(data) );
		if ( 0 > size ) {
			close( fd );
			return EPTMD_FAILED;
		}
		else if ( 0 == size ) {
			break;
		}
		else {}
		
		int result = WriteData( (unsigned char *)data, size );
		if ( EPTMD_SUCCESS != result ) {
			close( fd );
			return EPTMD_FAILED;
		}
	}
	
	if ( close( fd ) < 0 ) {
		return EPTMD_FAILED;
	}
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Read data from file descriptor.
 *-------------------------------------------------------------------------------------------------------------------*/
static int ReadUserFile(int fd, void *p_buffer, int buffer_size)
{
	int		total_size = 0;
	char*	p_data     = (char*)p_buffer;
	
	for ( ; buffer_size > total_size; ) {
		
		int read_size = (int)read( fd, p_data + total_size, (buffer_size - total_size) );
		if ( 0 > read_size ) {
			return EPTMD_FAILED;
		}
		else if ( 0 == read_size ) {
			break;
		}
		else {}
		
		total_size += read_size;
	}
	
	return total_size;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Feed paper.
 *-------------------------------------------------------------------------------------------------------------------*/
static int FeedPaper(EPTMS_CONFIG_T* p_config, cups_page_header_t* p_header, unsigned num_line)
{
	unsigned char	Command[3] = { ESC, 'J', 0xFF };
	unsigned		point = 0;
	double			integral = 0.0;
	
	double correction = (double)(num_line * p_config->v_motionUnit) / (double)p_header->HWResolution[1];
	double fractional = modf( correction, &integral );
	if ( fractional == 0 ) {} // warning
	
	point = (unsigned)integral;
	if ( 0 == point ) {
		return EPTMD_SUCCESS;
	}
	
	int result = EPTMD_SUCCESS;
	for ( ; 0xff < point; point -= 0xff ) {
		result = WriteData( Command, sizeof(Command) );
		if ( EPTMD_SUCCESS != result ) { return result; }
	}
	
	if ( 0 < point ) {
		Command[2] = (unsigned char)point;
		result = WriteData( Command, sizeof(Command) );
		if ( EPTMD_SUCCESS != result ) { return result; }
	}
	
	return EPTMD_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------
 * Write data to file descriptor.
 *-------------------------------------------------------------------------------------------------------------------*/
static int WriteData(unsigned char *p_buffer, unsigned int size)
{
	char*	p_data = (char*)p_buffer;
	int		result = 0;
	
	unsigned int count;
	for ( count = 0; size > count; count += result ) {
		result = (int)write( STDOUT_FILENO, (p_data + count), (size - count) );
		if ( 0 == result ) {
			break;
		}
		else if ( 0 > result) {
			if ( EINTR == errno ) {
				continue;
			}
			return -1;
		}
		else {}
	}
	
	return (count == size) ? EPTMD_SUCCESS : EPTMD_FAILED;
}
/*-------------------------------------------------------------------------------------------------------------------*/
