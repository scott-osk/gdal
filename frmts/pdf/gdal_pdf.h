/******************************************************************************
 * $Id$
 *
 * Project:  PDF Translator
 * Purpose:  Definition of classes for OGR .pdf driver.
 * Author:   Even Rouault, even dot rouault at spatialys.com
 *
 ******************************************************************************
 *
 * Support for open-source PDFium library
 *
 * Copyright (C) 2015 Klokan Technologies GmbH (http://www.klokantech.com/)
 * Author: Martin Mikita <martin.mikita@klokantech.com>, xmikit00 @ FIT VUT Brno
 *
 ******************************************************************************
 * Copyright (c) 2010-2014, Even Rouault <even dot rouault at spatialys.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#ifndef GDAL_PDF_H_INCLUDED
#define GDAL_PDF_H_INCLUDED

/* hack for PDF driver and poppler >= 0.15.0 that defines incompatible "typedef bool GBool" */
/* in include/poppler/goo/gtypes.h with the one defined in cpl_port.h */
#define CPL_GBOOL_DEFINED
#define OGR_FEATURESTYLE_INCLUDE
#include "cpl_port.h"

#include <map>
#include <set>
#include <stack>
#include <utility>
#include <bitset>   // For detecting usage of PDF library
#include <algorithm>

#include "pdfsdk_headers.h"

#include "gdal_pam.h"
#include "ogrsf_frmts.h"

#include "ogr_mem.h"
#include "pdfobject.h"

#define     PDFLIB_POPPLER    0
#define     PDFLIB_PODOFO     1
#define     PDFLIB_PDFIUM     2
#define     PDFLIB_COUNT      3

#if defined(HAVE_POPPLER) || defined(HAVE_PODOFO) || defined(HAVE_PDFIUM)
#define HAVE_PDF_READ_SUPPORT
#endif

/************************************************************************/
/*                             OGRPDFLayer                              */
/************************************************************************/

#ifdef HAVE_PDF_READ_SUPPORT

class PDFDataset;

class OGRPDFLayer final: public OGRMemLayer
{
    PDFDataset       *poDS;
    int               bGeomTypeSet;
    int               bGeomTypeMixed;

public:
        OGRPDFLayer( PDFDataset* poDS,
                     const char * pszName,
                     OGRSpatialReference *poSRS,
                     OGRwkbGeometryType eGeomType );

    void                Fill( GDALPDFArray* poArray );

    virtual int                 TestCapability( const char * ) override;
};

#endif

/************************************************************************/
/*                          OGRPDFWritableLayer                         */
/************************************************************************/

class PDFWritableVectorDataset;

class OGRPDFWritableLayer final: public OGRMemLayer
{
    PDFWritableVectorDataset       *poDS;

public:
        OGRPDFWritableLayer(PDFWritableVectorDataset* poDS,
                    const char * pszName,
                    OGRSpatialReference *poSRS,
                    OGRwkbGeometryType eGeomType);

    virtual int                 TestCapability( const char * ) override;
    virtual OGRErr              ICreateFeature( OGRFeature *poFeature ) override;
};

/************************************************************************/
/*                            GDALPDFTileDesc                           */
/************************************************************************/

typedef struct
{
    GDALPDFObject* poImage;
    double         adfCM[6];
    double         dfWidth;
    double         dfHeight;
    int            nBands;
} GDALPDFTileDesc;

#ifdef HAVE_PDFIUM
/**
 * Structures for Document and Document's Page for PDFium library,
 *  which does not support multi-threading.
 * Structures keeps objects for PDFium library and exclusive mutex locks
 *  for one-per-time access of PDFium library methods with multi-threading GDAL
 * Structures also keeps only one object per each opened PDF document
 *  - this saves time for opening and memory for opened objects
 * Document is closed after closing all pages object.
 */

/************************************************************************/
/*                           TPdfiumPageStruct                          */
/************************************************************************/

// Map of Pdfium pages in following structure
typedef struct {
  int pageNum;
  CPDF_Page* page;
  CPLMutex * readMutex;
  int sharedNum;
} TPdfiumPageStruct;

typedef std::map<int, TPdfiumPageStruct*>        TMapPdfiumPages;

/************************************************************************/
/*                         TPdfiumDocumentStruct                        */
/************************************************************************/

// Structure for Mutex on File
typedef struct {
  char* filename;
  CPDF_Document* doc;
  TMapPdfiumPages pages;
  FPDF_FILEACCESS* psFileAccess;
} TPdfiumDocumentStruct;

#endif  // ~ HAVE_PDFIUM

/************************************************************************/
/* ==================================================================== */
/*                              PDFDataset                              */
/* ==================================================================== */
/************************************************************************/

class PDFRasterBand;
class PDFImageRasterBand;

#ifdef HAVE_POPPLER
class ObjectAutoFree;
#endif

#define MAX_TOKEN_SIZE 256
#define TOKEN_STACK_SIZE 8

#ifdef HAVE_PDF_READ_SUPPORT

class PDFDataset final: public GDALPamDataset
{
    friend class PDFRasterBand;
    friend class PDFImageRasterBand;

    VSILFILE    *m_fp = nullptr;
    PDFDataset*  poParentDS;

    CPLString    osFilename;
    CPLString    osUserPwd;
    OGRSpatialReference m_oSRS{};
    double       dfDPI;
    int          bHasCTM;
    double       adfCTM[6];
    double       adfGeoTransform[6];
    int          bGeoTransformValid;
    int          nGCPCount;
    GDAL_GCP    *pasGCPList;
    int          bProjDirty;
    int          bNeatLineDirty;

    GDALMultiDomainMetadata oMDMD_PDF;
    int          bInfoDirty;
    int          bXMPDirty;

    std::bitset<PDFLIB_COUNT> bUseLib;
#ifdef HAVE_POPPLER
    PDFDoc*      poDocPoppler;
#endif
#ifdef HAVE_PODOFO
    PoDoFo::PdfMemDocument* poDocPodofo;
    int          bPdfToPpmFailed;
#endif
#ifdef HAVE_PDFIUM
    TPdfiumDocumentStruct*  poDocPdfium;
    TPdfiumPageStruct*      poPagePdfium;
    std::vector<PDFDataset*> apoOvrDS, apoOvrDSBackup;
#endif
    GDALPDFObject* poPageObj;

    int          iPage;

    GDALPDFObject *poImageObj;

    double       dfMaxArea;
    int          ParseLGIDictObject(GDALPDFObject* poLGIDict);
    int          ParseLGIDictDictFirstPass(GDALPDFDictionary* poLGIDict, int* pbIsBestCandidate = nullptr);
    int          ParseLGIDictDictSecondPass(GDALPDFDictionary* poLGIDict);
    int          ParseProjDict(GDALPDFDictionary* poProjDict);
    int          ParseVP(GDALPDFObject* poVP, double dfMediaBoxWidth, double dfMediaBoxHeight);
    int          ParseMeasure(GDALPDFObject* poMeasure,
                              double dfMediaBoxWidth, double dfMediaBoxHeight,
                              double dfULX, double dfULY, double dfLRX, double dfLRY);

    int          bTried;
    GByte       *pabyCachedData;
    int          nLastBlockXOff;
    int          nLastBlockYOff;

    OGRPolygon*  poNeatLine;

    std::vector<GDALPDFTileDesc> asTiles; /* in the order of the PDF file */
    std::vector<int> aiTiles; /* in the order of blocks */
    int          nBlockXSize;
    int          nBlockYSize;
    int          CheckTiledRaster();

    void         GuessDPI(GDALPDFDictionary* poPageDict, int* pnBands);
    void         FindXMP(GDALPDFObject* poObj);
    void         ParseInfo(GDALPDFObject* poObj);

#ifdef HAVE_POPPLER
    ObjectAutoFree* poCatalogObjectPoppler;
#endif
    GDALPDFObject* poCatalogObject;
    GDALPDFObject* GetCatalog();

#if defined(HAVE_POPPLER) || defined(HAVE_PDFIUM)
    void         AddLayer(const char* pszLayerName);
#endif

#if defined(HAVE_POPPLER)
    void         ExploreLayersPoppler(GDALPDFArray* poArray, CPLString osTopLayer, int nRecLevel, int& nVisited, bool& bStop);
    void         FindLayersPoppler();
    void         TurnLayersOnOffPoppler();
    std::vector<std::pair<CPLString, OptionalContentGroup*> > oLayerOCGListPoppler;
#endif

#ifdef HAVE_PDFIUM
    void         ExploreLayersPdfium(GDALPDFArray* poArray, int nRecLevel, CPLString osTopLayer = "");
    void         FindLayersPdfium();
    void         PDFiumRenderPageBitmap(FPDF_BITMAP bitmap, FPDF_PAGE page, int start_x, int start_y,
                                        int size_x, int size_y, const char* pszRenderingOptions);
    void         TurnLayersOnOffPdfium();

public:
    typedef enum
    {
        VISIBILITY_DEFAULT,
        VISIBILITY_ON,
        VISIBILITY_OFF
    } VisibilityState;

    VisibilityState GetVisibilityStateForOGCPdfium(int nNum, int nGen);

private:
    std::map< CPLString, std::pair<int,int> > oMapLayerNameToOCGNumGenPdfium;
    std::map< std::pair<int,int>, VisibilityState > oMapOCGNumGenToVisibilityStatePdfium;
#endif

    CPLStringList osLayerList;

    struct LayerWithRef
    {
        CPLString        osName{};
        GDALPDFObjectNum nOCGNum{};
        int              nOCGGen = 0;

        LayerWithRef(const CPLString& osNameIn,
                     const GDALPDFObjectNum& nOCGNumIn,
                     int nOCGGenIn) :
            osName(osNameIn), nOCGNum(nOCGNumIn), nOCGGen(nOCGGenIn) {}
    };
    std::vector<LayerWithRef> aoLayerWithRef;

    CPLString     FindLayerOCG(GDALPDFDictionary* poPageDict,
                               const char* pszLayerName);
    void          FindLayersGeneric(GDALPDFDictionary* poPageDict);

    int          bUseOCG;

    static const char*  GetOption(char** papszOpenOptions,
                                  const char* pszOptionName,
                                  const char* pszDefaultVal);

    int                 bHasLoadedLayers;
    int                 nLayers;
    OGRLayer          **papoLayers;

    double              dfPageWidth;
    double              dfPageHeight;
    void                PDFCoordsToSRSCoords(double x, double y,
                                             double& X, double &Y);

    std::map<int,OGRGeometry*> oMapMCID;
    void                CleanupIntermediateResources();

    std::map<CPLString, int> oMapOperators;
    void                InitMapOperators();

    int                 bSetStyle;

    void                ExploreTree(GDALPDFObject* poObj,
                                    std::set< std::pair<int,int> > aoSetAlreadyVisited,
                                    int nRecLevel);
    void                ExploreContents(GDALPDFObject* poObj, GDALPDFObject* poResources, int nDepth, int& nVisited, bool& bStop);

    void                ExploreContentsNonStructuredInternal(GDALPDFObject* poContents,
                                                             GDALPDFObject* poResources,
                                                             std::map<CPLString, OGRPDFLayer*>& oMapPropertyToLayer,
                                                             OGRPDFLayer* poSingleLayer);
    void                ExploreContentsNonStructured(GDALPDFObject* poObj, GDALPDFObject* poResources);

    int                 UnstackTokens(const char* pszToken,
                                      int nRequiredArgs,
                                      char aszTokenStack[TOKEN_STACK_SIZE][MAX_TOKEN_SIZE],
                                      int& nTokenStackSize,
                                      double* adfCoords);
    OGRGeometry*        ParseContent(const char* pszContent,
                                     GDALPDFObject* poResources,
                                     int bInitBDCStack,
                                     int bMatchQ,
                                     std::map<CPLString, OGRPDFLayer*>& oMapPropertyToLayer,
                                     OGRPDFLayer* poCurLayer);
    OGRGeometry*        BuildGeometry(std::vector<double>& oCoords,
                                      int bHasFoundFill,
                                      int bHasMultiPart);

    int                 OpenVectorLayers(GDALPDFDictionary* poPageDict);

#ifdef HAVE_PDFIUM
    void    InitOverviews();
#endif  // ~ HAVE_PDFIUM

  public:
                 PDFDataset(PDFDataset* poParentDS = nullptr, int nXSize = 0, int nYSize = 0);
    virtual     ~PDFDataset();

    virtual CPLErr GetGeoTransform( double * ) override;

    virtual CPLErr      SetGeoTransform(double* padfGeoTransform) override;

    const OGRSpatialReference* GetSpatialRef() const override;
    CPLErr SetSpatialRef(const OGRSpatialReference* poSRS) override;

    virtual char      **GetMetadataDomainList() override;
    virtual char      **GetMetadata( const char * pszDomain = "" ) override;
    virtual CPLErr      SetMetadata( char ** papszMetadata,
                                     const char * pszDomain = "" ) override;
    virtual const char *GetMetadataItem( const char * pszName,
                                         const char * pszDomain = "" ) override;
    virtual CPLErr      SetMetadataItem( const char * pszName,
                                         const char * pszValue,
                                         const char * pszDomain = "" ) override;

    virtual CPLErr IRasterIO( GDALRWFlag, int, int, int, int,
                              void *, int, int, GDALDataType,
                              int, int *,
                              GSpacing nPixelSpace, GSpacing nLineSpace,
                              GSpacing nBandSpace,
                              GDALRasterIOExtraArg* psExtraArg) override;

    virtual int    GetGCPCount() override;
    const OGRSpatialReference* GetGCPSpatialRef() const override;
    virtual const GDAL_GCP *GetGCPs() override;
    CPLErr SetGCPs( int nGCPCountIn, const GDAL_GCP *pasGCPListIn,
                    const OGRSpatialReference* poSRS ) override;

    CPLErr ReadPixels( int nReqXOff, int nReqYOff,
                       int nReqXSize, int nReqYSize,
                       GSpacing nPixelSpace,
                       GSpacing nLineSpace,
                       GSpacing nBandSpace,
                       GByte* pabyData );

    virtual int                 GetLayerCount() override;
    virtual OGRLayer*           GetLayer( int ) override;

    virtual int                 TestCapability( const char * ) override;

    OGRGeometry        *GetGeometryFromMCID(int nMCID);

    GDALPDFObject*      GetPageObj() { return poPageObj; }
    double              GetPageWidth() const { return dfPageWidth; }
    double              GetPageHeight() const { return dfPageHeight; }

    static PDFDataset  *Open( GDALOpenInfo * );
    static GDALDataset *OpenWrapper( GDALOpenInfo * poOpenInfo ) { return Open(poOpenInfo); }
    static int          Identify( GDALOpenInfo * );

#ifdef HAVE_PDFIUM
    virtual CPLErr IBuildOverviews( const char *,
                                    int, const int *,
                                    int, const int *,
                                    GDALProgressFunc, void *,
                                    CSLConstList papszOptions ) override;

    static int bPdfiumInit;
#endif
};

/************************************************************************/
/* ==================================================================== */
/*                         PDFRasterBand                                */
/* ==================================================================== */
/************************************************************************/

class PDFRasterBand CPL_NON_FINAL: public GDALPamRasterBand
{
    friend class PDFDataset;

    int   nResolutionLevel;

    CPLErr IReadBlockFromTile( int, int, void * );

  public:

                PDFRasterBand( PDFDataset *, int, int );
    virtual ~PDFRasterBand();

    virtual GDALSuggestedBlockAccessPattern GetSuggestedBlockAccessPattern() const override;

#ifdef HAVE_PDFIUM
    virtual int    GetOverviewCount() override;
    virtual GDALRasterBand *GetOverview( int ) override;
#endif  // ~ HAVE_PDFIUM

    virtual CPLErr IReadBlock( int, int, void * ) override;
    virtual GDALColorInterp GetColorInterpretation() override;

#ifdef notdef
    virtual CPLErr IRasterIO( GDALRWFlag, int, int, int, int,
                              void *, int, int, GDALDataType,
                              GSpacing nPixelSpace, GSpacing nLineSpace,
                              GDALRasterIOExtraArg* psExtraArg) override;
#endif
};

#endif /* HAVE_PDF_READ_SUPPORT */

/************************************************************************/
/*                          PDFWritableDataset                          */
/************************************************************************/

class PDFWritableVectorDataset final: public GDALDataset
{
        char**              papszOptions;

        int                 nLayers;
        OGRLayer          **papoLayers;

        int                 bModified;

    public:
                            PDFWritableVectorDataset();
        virtual ~PDFWritableVectorDataset();

        virtual OGRLayer*           ICreateLayer( const char * pszLayerName,
                                                OGRSpatialReference *poSRS,
                                                OGRwkbGeometryType eType,
                                                char ** papszOptions ) override;

        virtual OGRErr              SyncToDisk();

        virtual int                 GetLayerCount() override;
        virtual OGRLayer*           GetLayer( int ) override;

        virtual int                 TestCapability( const char * ) override;

        static GDALDataset* Create( const char * pszName,
                                 int nXSize, int nYSize, int nBands,
                                 GDALDataType eType, char ** papszOptions );

        void                SetModified() { bModified = TRUE; }
};

GDALDataset* GDALPDFOpen(const char* pszFilename, GDALAccess eAccess);
CPLString PDFSanitizeLayerName(const char* pszName);

#endif /* ndef GDAL_PDF_H_INCLUDED */
