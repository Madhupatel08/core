/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <vcl/canvastools.hxx>
#include <vcl/pdfextoutdevdata.hxx>
#include <vcl/graph.hxx>
#include <vcl/outdev.hxx>
#include <vcl/gfxlink.hxx>
#include <vcl/metaact.hxx>
#include <vcl/graphicfilter.hxx>
#include <basegfx/polygon/b2dpolygon.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include <sal/log.hxx>
#include <o3tl/safeint.hxx>
#include <osl/diagnose.h>
#include <tools/stream.hxx>

#include <memory>
#include <map>

namespace vcl
{
namespace {

struct PDFExtOutDevDataSync
{
    enum Action{    CreateNamedDest,
                    CreateDest,
                    CreateLink,
                    CreateScreen,
                    SetLinkDest,
                    SetLinkURL,
                    SetScreenURL,
                    SetScreenStream,
                    RegisterDest,
                    CreateOutlineItem,
                    CreateNote,
                    SetPageTransition,

                    EnsureStructureElement,
                    InitStructureElement,
                    BeginStructureElement,
                    EndStructureElement,
                    SetCurrentStructureElement,
                    SetStructureAttribute,
                    SetStructureAttributeNumerical,
                    SetStructureBoundingBox,
                    SetStructureAnnotIds,
                    SetActualText,
                    SetAlternateText,
                    CreateControl,
                    BeginGroup,
                    EndGroupGfxLink
    };

    sal_uInt32  nIdx;
    Action      eAct;
};

struct PDFLinkDestination
{
    tools::Rectangle               mRect;
    MapMode                 mMapMode;
    sal_Int32               mPageNr;
    PDFWriter::DestAreaType mAreaType;
};

}

struct GlobalSyncData
{
    std::deque< PDFExtOutDevDataSync::Action >  mActions;
    std::deque< MapMode >                       mParaMapModes;
    std::deque< tools::Rectangle >                     mParaRects;
    std::deque< sal_Int32 >                     mParaInts;
    std::deque< sal_uInt32 >                    mParauInts;
    std::deque< OUString >                 mParaOUStrings;
    std::deque< PDFWriter::DestAreaType >       mParaDestAreaTypes;
    std::deque< PDFNote >                       mParaPDFNotes;
    std::deque< PDFWriter::PageTransition >     mParaPageTransitions;
    ::std::map< sal_Int32, PDFLinkDestination > mFutureDestinations;

    sal_Int32 GetMappedId();

    /** the way this appears to work: (only) everything that increments mCurId
        at recording time must put an item into mParaIds at playback time,
        so that the mCurId becomes the eventual index into mParaIds.
     */
    sal_Int32                   mCurId;
    std::vector< sal_Int32 >    mParaIds;
    std::map<void const*, sal_Int32> mSEMap;

    sal_Int32                   mCurrentStructElement;
    std::vector< sal_Int32 >    mStructParents;
    GlobalSyncData() :
            mCurId ( 0 ),
            mCurrentStructElement( 0 )
    {
        mStructParents.push_back(0); // because PDFWriterImpl has a dummy root
    }
    void PlayGlobalActions( PDFWriter& rWriter );
};

sal_Int32 GlobalSyncData::GetMappedId()
{
    sal_Int32 nLinkId = mParaInts.front();
    mParaInts.pop_front();

    /*  negative values are intentionally passed as invalid IDs
     *  e.g. to create a new top level outline item
     */
    if( nLinkId >= 0 )
    {
        if ( o3tl::make_unsigned(nLinkId) < mParaIds.size() )
            nLinkId = mParaIds[ nLinkId ];
        else
            nLinkId = -1;

        SAL_WARN_IF( nLinkId < 0, "vcl", "unmapped id in GlobalSyncData" );
    }

    return nLinkId;
}

void GlobalSyncData::PlayGlobalActions( PDFWriter& rWriter )
{
    for (auto const& action : mActions)
    {
        switch (action)
        {
            case PDFExtOutDevDataSync::CreateNamedDest : //i56629
            {
                rWriter.Push( PushFlags::MAPMODE );
                rWriter.SetMapMode( mParaMapModes.front() );
                mParaMapModes.pop_front();
                mParaIds.push_back( rWriter.CreateNamedDest( mParaOUStrings.front(), mParaRects.front(), mParaInts.front(), mParaDestAreaTypes.front() ) );
                mParaOUStrings.pop_front();
                mParaRects.pop_front();
                mParaInts.pop_front();
                mParaDestAreaTypes.pop_front();
                rWriter.Pop();
            }
            break;
            case PDFExtOutDevDataSync::CreateDest :
            {
                rWriter.Push( PushFlags::MAPMODE );
                rWriter.SetMapMode( mParaMapModes.front() );
                mParaMapModes.pop_front();
                mParaIds.push_back( rWriter.CreateDest( mParaRects.front(), mParaInts.front(), mParaDestAreaTypes.front() ) );
                mParaRects.pop_front();
                mParaInts.pop_front();
                mParaDestAreaTypes.pop_front();
                rWriter.Pop();
            }
            break;
            case PDFExtOutDevDataSync::CreateLink :
            {
                rWriter.Push( PushFlags::MAPMODE );
                rWriter.SetMapMode( mParaMapModes.front() );
                mParaMapModes.pop_front();
                mParaIds.push_back( rWriter.CreateLink(mParaRects.front(), mParaInts.front(), mParaOUStrings.front()) );
                // resolve LinkAnnotation structural attribute
                rWriter.SetLinkPropertyID( mParaIds.back(), sal_Int32(mParaIds.size()-1) );
                mParaRects.pop_front();
                mParaInts.pop_front();
                mParaOUStrings.pop_front();
                rWriter.Pop();
            }
            break;
            case PDFExtOutDevDataSync::CreateScreen:
            {
                rWriter.Push(PushFlags::MAPMODE);
                rWriter.SetMapMode(mParaMapModes.front());
                mParaMapModes.pop_front();
                OUString const altText(mParaOUStrings.front());
                mParaOUStrings.pop_front();
                OUString const mimeType(mParaOUStrings.front());
                mParaOUStrings.pop_front();
                mParaIds.push_back(rWriter.CreateScreen(mParaRects.front(), mParaInts.front(), altText, mimeType));
                // resolve AnnotIds structural attribute
                rWriter.SetLinkPropertyID(mParaIds.back(), sal_Int32(mParaIds.size()-1));
                mParaRects.pop_front();
                mParaInts.pop_front();
                rWriter.Pop();
            }
            break;
            case PDFExtOutDevDataSync::SetLinkDest :
            {
                sal_Int32 nLinkId = GetMappedId();
                sal_Int32 nDestId = GetMappedId();
                rWriter.SetLinkDest( nLinkId, nDestId );
            }
            break;
            case PDFExtOutDevDataSync::SetLinkURL :
            {
                sal_Int32 nLinkId = GetMappedId();
                rWriter.SetLinkURL( nLinkId, mParaOUStrings.front() );
                mParaOUStrings.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::SetScreenURL:
            {
                sal_Int32 nScreenId = GetMappedId();
                rWriter.SetScreenURL(nScreenId, mParaOUStrings.front());
                mParaOUStrings.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::SetScreenStream:
            {
                sal_Int32 nScreenId = GetMappedId();
                rWriter.SetScreenStream(nScreenId, mParaOUStrings.front());
                mParaOUStrings.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::RegisterDest :
            {
                const sal_Int32 nDestId = mParaInts.front();
                mParaInts.pop_front();
                OSL_ENSURE( mFutureDestinations.find( nDestId ) != mFutureDestinations.end(),
                    "GlobalSyncData::PlayGlobalActions: DescribeRegisteredRequest has not been called for that destination!" );

                PDFLinkDestination& rDest = mFutureDestinations[ nDestId ];

                rWriter.Push( PushFlags::MAPMODE );
                rWriter.SetMapMode( rDest.mMapMode );
                mParaIds.push_back( rWriter.RegisterDestReference( nDestId, rDest.mRect, rDest.mPageNr, rDest.mAreaType ) );
                rWriter.Pop();
            }
            break;
            case PDFExtOutDevDataSync::CreateOutlineItem :
            {
                sal_Int32 nParent = GetMappedId();
                sal_Int32 nLinkId = GetMappedId();
                mParaIds.push_back( rWriter.CreateOutlineItem( nParent, mParaOUStrings.front(), nLinkId ) );
                mParaOUStrings.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::CreateNote :
            {
                rWriter.Push( PushFlags::MAPMODE );
                rWriter.SetMapMode( mParaMapModes.front() );
                rWriter.CreateNote( mParaRects.front(), mParaPDFNotes.front(), mParaInts.front() );
                mParaMapModes.pop_front();
                mParaRects.pop_front();
                mParaPDFNotes.pop_front();
                mParaInts.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::SetPageTransition :
            {
                rWriter.SetPageTransition( mParaPageTransitions.front(), mParauInts.front(), mParaInts.front() );
                mParaPageTransitions.pop_front();
                mParauInts.pop_front();
                mParaInts.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::EnsureStructureElement:
            case PDFExtOutDevDataSync::InitStructureElement:
            case PDFExtOutDevDataSync::BeginStructureElement:
            case PDFExtOutDevDataSync::EndStructureElement:
            case PDFExtOutDevDataSync::SetCurrentStructureElement:
            case PDFExtOutDevDataSync::SetStructureAttribute:
            case PDFExtOutDevDataSync::SetStructureAttributeNumerical:
            case PDFExtOutDevDataSync::SetStructureBoundingBox:
            case PDFExtOutDevDataSync::SetStructureAnnotIds:
            case PDFExtOutDevDataSync::SetActualText:
            case PDFExtOutDevDataSync::SetAlternateText:
            case PDFExtOutDevDataSync::CreateControl:
            case PDFExtOutDevDataSync::BeginGroup:
            case PDFExtOutDevDataSync::EndGroupGfxLink:
                break;
        }
    }
}

struct PageSyncData
{
    std::deque< PDFExtOutDevDataSync >              mActions;
    std::deque< tools::Rectangle >                         mParaRects;
    std::deque< sal_Int32 >                         mParaInts;
    std::deque< OUString >                     mParaOUStrings;
    std::deque< PDFWriter::StructElement >          mParaStructElements;
    std::deque< PDFWriter::StructAttribute >        mParaStructAttributes;
    std::deque< PDFWriter::StructAttributeValue >   mParaStructAttributeValues;
    std::deque< Graphic >                           mGraphics;
    Graphic                                         mCurrentGraphic;
    std::deque< std::shared_ptr< PDFWriter::AnyWidget > >
                                                    mControls;
    GlobalSyncData*                                 mpGlobalData;

    bool                                        mbGroupIgnoreGDIMtfActions;


    explicit PageSyncData( GlobalSyncData* pGlobal )
        : mbGroupIgnoreGDIMtfActions ( false )
    { mpGlobalData = pGlobal; }

    void PushAction( const OutputDevice& rOutDev, const PDFExtOutDevDataSync::Action eAct );
    bool PlaySyncPageAct( PDFWriter& rWriter, sal_uInt32& rCurGDIMtfAction, const GDIMetaFile& rMtf, const PDFExtOutDevData& rOutDevData );
};

void PageSyncData::PushAction( const OutputDevice& rOutDev, const PDFExtOutDevDataSync::Action eAct )
{
    GDIMetaFile* pMtf = rOutDev.GetConnectMetaFile();
    SAL_WARN_IF( !pMtf, "vcl", "PageSyncData::PushAction -> no ConnectMetaFile !!!" );

    PDFExtOutDevDataSync aSync;
    aSync.eAct = eAct;
    if ( pMtf )
        aSync.nIdx = pMtf->GetActionSize();
    else
        aSync.nIdx = 0x7fffffff;    // sync not possible
    mActions.push_back( aSync );
}
bool PageSyncData::PlaySyncPageAct( PDFWriter& rWriter, sal_uInt32& rCurGDIMtfAction, const GDIMetaFile& rMtf, const PDFExtOutDevData& rOutDevData )
{
    bool bRet = false;
    if ( !mActions.empty() && ( mActions.front().nIdx == rCurGDIMtfAction ) )
    {
        bRet = true;
        PDFExtOutDevDataSync aDataSync = mActions.front();
        mActions.pop_front();
        switch( aDataSync.eAct )
        {
            case PDFExtOutDevDataSync::EnsureStructureElement:
            {
#ifndef NDEBUG
                sal_Int32 const id =
#endif
                    rWriter.EnsureStructureElement();
                assert(id == -1 || id == mParaInts.front()); // identity mapping
                mParaInts.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::InitStructureElement:
            {
                rWriter.InitStructureElement(mParaInts.front(), mParaStructElements.front(), mParaOUStrings.front());
                mParaInts.pop_front();
                mParaStructElements.pop_front();
                mParaOUStrings.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::BeginStructureElement :
            {
                rWriter.BeginStructureElement(mParaInts.front());
                mParaInts.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::EndStructureElement :
            {
                rWriter.EndStructureElement();
            }
            break;
            case PDFExtOutDevDataSync::SetCurrentStructureElement:
            {
                rWriter.SetCurrentStructureElement(mParaInts.front());
                mParaInts.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::SetStructureAttribute :
            {
                rWriter.SetStructureAttribute( mParaStructAttributes.front(), mParaStructAttributeValues.front() );
                mParaStructAttributeValues.pop_front();
                mParaStructAttributes.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::SetStructureAttributeNumerical :
            {
                rWriter.SetStructureAttributeNumerical( mParaStructAttributes.front(), mParaInts.front() );
                mParaStructAttributes.pop_front();
                mParaInts.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::SetStructureBoundingBox :
            {
                rWriter.SetStructureBoundingBox( mParaRects.front() );
                mParaRects.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::SetStructureAnnotIds:
            {
                ::std::vector<sal_Int32> annotIds;
                auto size(mParaInts.front());
                mParaInts.pop_front();
                for (auto i = 0; i < size; ++i)
                {
                    annotIds.push_back(mParaInts.front());
                    mParaInts.pop_front();
                }
                rWriter.SetStructureAnnotIds(annotIds);
            }
            break;
            case PDFExtOutDevDataSync::SetActualText :
            {
                rWriter.SetActualText( mParaOUStrings.front() );
                mParaOUStrings.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::SetAlternateText :
            {
                rWriter.SetAlternateText( mParaOUStrings.front() );
                mParaOUStrings.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::CreateControl:
            {
                std::shared_ptr< PDFWriter::AnyWidget > pControl( mControls.front() );
                SAL_WARN_IF( !pControl, "vcl", "PageSyncData::PlaySyncPageAct: invalid widget!" );
                if ( pControl )
                {
                    sal_Int32 const n = rWriter.CreateControl(*pControl);
                    // resolve AnnotIds structural attribute
                    ::std::vector<sal_Int32> const annotIds{ sal_Int32(mpGlobalData->mParaIds.size()) };
                    rWriter.SetStructureAnnotIds(annotIds);
                    rWriter.SetLinkPropertyID(n, sal_Int32(mpGlobalData->mParaIds.size()));
                    mpGlobalData->mParaIds.push_back(n);
                }
                mControls.pop_front();
            }
            break;
            case PDFExtOutDevDataSync::BeginGroup :
            {
                /* first determining if this BeginGroup is starting a GfxLink,
                   by searching for an EndGroup or an EndGroupGfxLink */
                mbGroupIgnoreGDIMtfActions = false;
                auto isStartingGfxLink = std::any_of(mActions.begin(), mActions.end(),
                    [](const PDFExtOutDevDataSync& rAction) { return rAction.eAct == PDFExtOutDevDataSync::EndGroupGfxLink; });
                if ( isStartingGfxLink )
                {
                    Graphic& rGraphic = mGraphics.front();
                    if ( rGraphic.IsGfxLink() && mParaRects.size() >= 2 )
                    {
                        GfxLinkType eType = rGraphic.GetGfxLink().GetType();
                        if ( eType == GfxLinkType::NativeJpg )
                        {
                            mbGroupIgnoreGDIMtfActions = rOutDevData.HasAdequateCompression(rGraphic, mParaRects[0], mParaRects[1]);
                            if ( !mbGroupIgnoreGDIMtfActions )
                                mCurrentGraphic = rGraphic;
                        }
                        else if ( eType == GfxLinkType::NativePng || eType == GfxLinkType::NativePdf )
                        {
                            if ( eType == GfxLinkType::NativePdf || rOutDevData.HasAdequateCompression(rGraphic, mParaRects[0], mParaRects[1]) )
                                mCurrentGraphic = rGraphic;
                        }
                    }
                }
            }
            break;
            case PDFExtOutDevDataSync::EndGroupGfxLink :
            {
                tools::Rectangle aOutputRect, aVisibleOutputRect;
                Graphic   aGraphic( mGraphics.front() );

                mGraphics.pop_front();
                sal_Int32 nTransparency = mParaInts.front();
                mParaInts.pop_front();
                aOutputRect = mParaRects.front();
                mParaRects.pop_front();
                aVisibleOutputRect = mParaRects.front();
                mParaRects.pop_front();

                if ( mbGroupIgnoreGDIMtfActions )
                {
                    bool bClippingNeeded = ( aOutputRect != aVisibleOutputRect ) && !aVisibleOutputRect.IsEmpty();

                    GfxLink   aGfxLink( aGraphic.GetGfxLink() );
                    if ( aGfxLink.GetType() == GfxLinkType::NativeJpg )
                    {
                        if ( bClippingNeeded )
                        {
                            rWriter.Push();
                            basegfx::B2DPolyPolygon aRect( basegfx::utils::createPolygonFromRect(
                                vcl::unotools::b2DRectangleFromRectangle(aVisibleOutputRect) ) );
                            rWriter.SetClipRegion( aRect);
                        }

                        AlphaMask aAlphaMask;
                        if (nTransparency)
                        {
                            aAlphaMask = AlphaMask(aGraphic.GetSizePixel());
                            aAlphaMask.Erase(nTransparency);
                        }

                        SvMemoryStream aTmp;
                        const sal_uInt8* pData = aGfxLink.GetData();
                        sal_uInt32 nBytes = aGfxLink.GetDataSize();
                        if( pData && nBytes )
                        {
                            aTmp.WriteBytes( pData, nBytes );

                            // Look up the output rectangle from the previous
                            // bitmap scale action if possible. This has the
                            // correct position and size for images with a
                            // custom translation (Writer header) or scaling
                            // (Impress notes page).
                            if (rCurGDIMtfAction > 0)
                            {
                                const MetaAction* pAction = rMtf.GetAction(rCurGDIMtfAction - 1);
                                if (pAction && pAction->GetType() == MetaActionType::BMPSCALE)
                                {
                                    const MetaBmpScaleAction* pA
                                        = static_cast<const MetaBmpScaleAction*>(pAction);
                                    aOutputRect.SetPos(pA->GetPoint());
                                    aOutputRect.SetSize(pA->GetSize());
                                }
                            }
                            auto ePixelFormat = aGraphic.GetBitmapEx().getPixelFormat();
                            rWriter.DrawJPGBitmap(aTmp, ePixelFormat > vcl::PixelFormat::N8_BPP, aGraphic.GetSizePixel(), aOutputRect, aAlphaMask, aGraphic);
                        }

                        if ( bClippingNeeded )
                            rWriter.Pop();
                    }
                    mbGroupIgnoreGDIMtfActions = false;
                }
                mCurrentGraphic.Clear();
            }
            break;
            case PDFExtOutDevDataSync::CreateNamedDest:
            case PDFExtOutDevDataSync::CreateDest:
            case PDFExtOutDevDataSync::CreateLink:
            case PDFExtOutDevDataSync::CreateScreen:
            case PDFExtOutDevDataSync::SetLinkDest:
            case PDFExtOutDevDataSync::SetLinkURL:
            case PDFExtOutDevDataSync::SetScreenURL:
            case PDFExtOutDevDataSync::SetScreenStream:
            case PDFExtOutDevDataSync::RegisterDest:
            case PDFExtOutDevDataSync::CreateOutlineItem:
            case PDFExtOutDevDataSync::CreateNote:
            case PDFExtOutDevDataSync::SetPageTransition:
                break;
        }
    }
    else if ( mbGroupIgnoreGDIMtfActions )
    {
        rCurGDIMtfAction++;
        bRet = true;
    }
    return bRet;
}

PDFExtOutDevData::PDFExtOutDevData( const OutputDevice& rOutDev ) :
    mrOutDev                ( rOutDev ),
    mbTaggedPDF             ( false ),
    mbExportNotes           ( true ),
    mbExportNotesInMargin   ( false ),
    mbExportNotesPages      ( false ),
    mbTransitionEffects     ( true ),
    mbUseLosslessCompression( true ),
    mbReduceImageResolution ( false ),
    mbExportFormFields      ( false ),
    mbExportBookmarks       ( false ),
    mbExportHiddenSlides    ( false ),
    mbSinglePageSheets      ( false ),
    mbExportNDests          ( false ),
    mnPage                  ( -1 ),
    mnCompressionQuality    ( 90 ),
    mpGlobalSyncData        ( new GlobalSyncData() )
{
    mpPageSyncData.reset( new PageSyncData( mpGlobalSyncData.get() ) );
}

PDFExtOutDevData::~PDFExtOutDevData()
{
    mpPageSyncData.reset();
    mpGlobalSyncData.reset();
}

const Graphic& PDFExtOutDevData::GetCurrentGraphic() const
{
    return mpPageSyncData->mCurrentGraphic;
}

void PDFExtOutDevData::SetDocumentLocale( const css::lang::Locale& rLoc )
{
    maDocLocale = rLoc;
}
void PDFExtOutDevData::SetCurrentPageNumber( const sal_Int32 nPage )
{
    mnPage = nPage;
}
void PDFExtOutDevData::SetIsLosslessCompression( const bool bUseLosslessCompression )
{
    mbUseLosslessCompression = bUseLosslessCompression;
}
void PDFExtOutDevData::SetCompressionQuality( const sal_Int32 nQuality )
{
    mnCompressionQuality = nQuality;
}
void PDFExtOutDevData::SetIsReduceImageResolution( const bool bReduceImageResolution )
{
    mbReduceImageResolution = bReduceImageResolution;
}
void PDFExtOutDevData::SetIsExportNotes( const bool bExportNotes )
{
    mbExportNotes = bExportNotes;
}
void PDFExtOutDevData::SetIsExportNotesInMargin( const bool bExportNotesInMargin )
{
    mbExportNotesInMargin = bExportNotesInMargin;
}
void PDFExtOutDevData::SetIsExportNotesPages( const bool bExportNotesPages )
{
    mbExportNotesPages = bExportNotesPages;
}
void PDFExtOutDevData::SetIsExportTaggedPDF( const bool bTaggedPDF )
{
    mbTaggedPDF = bTaggedPDF;
}
void PDFExtOutDevData::SetIsExportTransitionEffects( const bool bTransitionEffects )
{
    mbTransitionEffects = bTransitionEffects;
}
void PDFExtOutDevData::SetIsExportFormFields( const bool bExportFomtFields )
{
    mbExportFormFields = bExportFomtFields;
}
void PDFExtOutDevData::SetIsExportBookmarks( const bool bExportBookmarks )
{
    mbExportBookmarks = bExportBookmarks;
}
void PDFExtOutDevData::SetIsExportHiddenSlides( const bool bExportHiddenSlides )
{
    mbExportHiddenSlides = bExportHiddenSlides;
}
void PDFExtOutDevData::SetIsSinglePageSheets( const bool bSinglePageSheets )
{
    mbSinglePageSheets = bSinglePageSheets;
}
void PDFExtOutDevData::SetIsExportNamedDestinations( const bool bExportNDests )
{
    mbExportNDests = bExportNDests;
}
void PDFExtOutDevData::ResetSyncData()
{
    *mpPageSyncData = PageSyncData( mpGlobalSyncData.get() );
}
bool PDFExtOutDevData::PlaySyncPageAct( PDFWriter& rWriter, sal_uInt32& rIdx, const GDIMetaFile& rMtf )
{
    return mpPageSyncData->PlaySyncPageAct( rWriter, rIdx, rMtf, *this );
}
void PDFExtOutDevData::PlayGlobalActions( PDFWriter& rWriter )
{
    mpGlobalSyncData->PlayGlobalActions( rWriter );
}

/* global actions, synchronisation to the recorded metafile isn't needed,
   all actions will be played after the last page was recorded
*/
//--->i56629
sal_Int32 PDFExtOutDevData::CreateNamedDest(const OUString& sDestName,  const tools::Rectangle& rRect, sal_Int32 nPageNr )
{
    mpGlobalSyncData->mActions.push_back( PDFExtOutDevDataSync::CreateNamedDest );
    mpGlobalSyncData->mParaOUStrings.push_back( sDestName );
    mpGlobalSyncData->mParaRects.push_back( rRect );
    mpGlobalSyncData->mParaMapModes.push_back( mrOutDev.GetMapMode() );
    mpGlobalSyncData->mParaInts.push_back( nPageNr == -1 ? mnPage : nPageNr );
    mpGlobalSyncData->mParaDestAreaTypes.push_back( PDFWriter::DestAreaType::XYZ );

    return mpGlobalSyncData->mCurId++;
}
//<---i56629
sal_Int32 PDFExtOutDevData::RegisterDest()
{
    const sal_Int32 nLinkDestID = mpGlobalSyncData->mCurId++;
    mpGlobalSyncData->mActions.push_back( PDFExtOutDevDataSync::RegisterDest );
    mpGlobalSyncData->mParaInts.push_back( nLinkDestID );

    return nLinkDestID;
}
void PDFExtOutDevData::DescribeRegisteredDest( sal_Int32 nDestId, const tools::Rectangle& rRect, sal_Int32 nPageNr, PDFWriter::DestAreaType eType )
{
    OSL_PRECOND( nDestId != -1, "PDFExtOutDevData::DescribeRegisteredDest: invalid destination Id!" );
    PDFLinkDestination aLinkDestination;
    aLinkDestination.mRect = rRect;
    aLinkDestination.mMapMode = mrOutDev.GetMapMode();
    aLinkDestination.mPageNr = nPageNr == -1 ? mnPage : nPageNr;
    aLinkDestination.mAreaType = eType;
    mpGlobalSyncData->mFutureDestinations[ nDestId ] = aLinkDestination;
}
sal_Int32 PDFExtOutDevData::CreateDest( const tools::Rectangle& rRect, sal_Int32 nPageNr, PDFWriter::DestAreaType eType )
{
    mpGlobalSyncData->mActions.push_back( PDFExtOutDevDataSync::CreateDest );
    mpGlobalSyncData->mParaRects.push_back( rRect );
    mpGlobalSyncData->mParaMapModes.push_back( mrOutDev.GetMapMode() );
    mpGlobalSyncData->mParaInts.push_back( nPageNr == -1 ? mnPage : nPageNr );
    mpGlobalSyncData->mParaDestAreaTypes.push_back( eType );
    return mpGlobalSyncData->mCurId++;
}
sal_Int32 PDFExtOutDevData::CreateLink(const tools::Rectangle& rRect, OUString const& rAltText, sal_Int32 nPageNr)
{
    mpGlobalSyncData->mActions.push_back( PDFExtOutDevDataSync::CreateLink );
    mpGlobalSyncData->mParaRects.push_back( rRect );
    mpGlobalSyncData->mParaMapModes.push_back( mrOutDev.GetMapMode() );
    mpGlobalSyncData->mParaInts.push_back( nPageNr == -1 ? mnPage : nPageNr );
    mpGlobalSyncData->mParaOUStrings.push_back(rAltText);
    return mpGlobalSyncData->mCurId++;
}

sal_Int32 PDFExtOutDevData::CreateScreen(const tools::Rectangle& rRect,
        OUString const& rAltText, OUString const& rMimeType,
        sal_Int32 nPageNr, SdrObject const*const pObj)
{
    mpGlobalSyncData->mActions.push_back(PDFExtOutDevDataSync::CreateScreen);
    mpGlobalSyncData->mParaRects.push_back(rRect);
    mpGlobalSyncData->mParaMapModes.push_back(mrOutDev.GetMapMode());
    mpGlobalSyncData->mParaInts.push_back(nPageNr);
    mpGlobalSyncData->mParaOUStrings.push_back(rAltText);
    mpGlobalSyncData->mParaOUStrings.push_back(rMimeType);
    auto const ret(mpGlobalSyncData->mCurId++);
    m_ScreenAnnotations[pObj].push_back(ret);
    return ret;
}

::std::vector<sal_Int32> const& PDFExtOutDevData::GetScreenAnnotIds(SdrObject const*const pObj) const
{
    auto const it(m_ScreenAnnotations.find(pObj));
    if (it == m_ScreenAnnotations.end())
    {
        assert(false); // expected?
    }
    return it->second;
}

void PDFExtOutDevData::SetLinkDest( sal_Int32 nLinkId, sal_Int32 nDestId )
{
    mpGlobalSyncData->mActions.push_back( PDFExtOutDevDataSync::SetLinkDest );
    mpGlobalSyncData->mParaInts.push_back( nLinkId );
    mpGlobalSyncData->mParaInts.push_back( nDestId );
}
void PDFExtOutDevData::SetLinkURL( sal_Int32 nLinkId, const OUString& rURL )
{
    mpGlobalSyncData->mActions.push_back( PDFExtOutDevDataSync::SetLinkURL );
    mpGlobalSyncData->mParaInts.push_back( nLinkId );
    mpGlobalSyncData->mParaOUStrings.push_back( rURL );
}

void PDFExtOutDevData::SetScreenURL(sal_Int32 nScreenId, const OUString& rURL)
{
    mpGlobalSyncData->mActions.push_back(PDFExtOutDevDataSync::SetScreenURL);
    mpGlobalSyncData->mParaInts.push_back(nScreenId);
    mpGlobalSyncData->mParaOUStrings.push_back(rURL);
}

void PDFExtOutDevData::SetScreenStream(sal_Int32 nScreenId, const OUString& rURL)
{
    mpGlobalSyncData->mActions.push_back(PDFExtOutDevDataSync::SetScreenStream);
    mpGlobalSyncData->mParaInts.push_back(nScreenId);
    mpGlobalSyncData->mParaOUStrings.push_back(rURL);
}

sal_Int32 PDFExtOutDevData::CreateOutlineItem( sal_Int32 nParent, const OUString& rText, sal_Int32 nDestID )
{
    if (nParent == -1)
        // Has no parent, it's a chapter / heading 1.
        maChapterNames.push_back(rText);

    mpGlobalSyncData->mActions.push_back( PDFExtOutDevDataSync::CreateOutlineItem );
    mpGlobalSyncData->mParaInts.push_back( nParent );
    mpGlobalSyncData->mParaOUStrings.push_back( rText );
    mpGlobalSyncData->mParaInts.push_back( nDestID );
    return mpGlobalSyncData->mCurId++;
}
void PDFExtOutDevData::CreateNote( const tools::Rectangle& rRect, const PDFNote& rNote, sal_Int32 nPageNr )
{
    mpGlobalSyncData->mActions.push_back( PDFExtOutDevDataSync::CreateNote );
    mpGlobalSyncData->mParaRects.push_back( rRect );
    mpGlobalSyncData->mParaMapModes.push_back( mrOutDev.GetMapMode() );
    mpGlobalSyncData->mParaPDFNotes.push_back( rNote );
    mpGlobalSyncData->mParaInts.push_back( nPageNr == -1 ? mnPage : nPageNr );
}
void PDFExtOutDevData::SetPageTransition( PDFWriter::PageTransition eType, sal_uInt32 nMilliSec )
{
    mpGlobalSyncData->mActions.push_back( PDFExtOutDevDataSync::SetPageTransition );
    mpGlobalSyncData->mParaPageTransitions.push_back( eType );
    mpGlobalSyncData->mParauInts.push_back( nMilliSec );
    mpGlobalSyncData->mParaInts.push_back( mnPage );
}

/* local (page), actions have to be played synchronously to the actions of
   of the recorded metafile (created by each xRenderable->render()) */

sal_Int32 PDFExtOutDevData::EnsureStructureElement(void const*const key)
{
    sal_Int32 id(-1);
    if (key != nullptr)
    {
        auto const it(mpGlobalSyncData->mSEMap.find(key));
        if (it != mpGlobalSyncData->mSEMap.end())
        {
            id = it->second;
        }
    }
    if (id == -1)
    {
        mpPageSyncData->PushAction(mrOutDev, PDFExtOutDevDataSync::EnsureStructureElement);
        id = mpGlobalSyncData->mStructParents.size();
        mpPageSyncData->mParaInts.push_back(id);
        mpGlobalSyncData->mStructParents.push_back(mpGlobalSyncData->mCurrentStructElement);
        if (key != nullptr)
        {
            mpGlobalSyncData->mSEMap.emplace(key, id);
        }
    }
    return id;
}

void PDFExtOutDevData::InitStructureElement(sal_Int32 const id,
        PDFWriter::StructElement const eType, const OUString& rAlias)
{
    mpPageSyncData->PushAction(mrOutDev, PDFExtOutDevDataSync::InitStructureElement);
    mpPageSyncData->mParaInts.push_back(id);
    mpPageSyncData->mParaStructElements.push_back( eType );
    mpPageSyncData->mParaOUStrings.push_back( rAlias );
}

void PDFExtOutDevData::BeginStructureElement(sal_Int32 const id)
{
    mpPageSyncData->PushAction( mrOutDev, PDFExtOutDevDataSync::BeginStructureElement );
    mpPageSyncData->mParaInts.push_back(id);
    mpGlobalSyncData->mCurrentStructElement = id;
}

sal_Int32 PDFExtOutDevData::WrapBeginStructureElement(
        PDFWriter::StructElement const eType, const OUString& rAlias)
{
    sal_Int32 const id = EnsureStructureElement(nullptr);
    InitStructureElement(id, eType, rAlias);
    BeginStructureElement(id);
    return id;
}

void PDFExtOutDevData::EndStructureElement()
{
    mpPageSyncData->PushAction( mrOutDev, PDFExtOutDevDataSync::EndStructureElement );
    mpGlobalSyncData->mCurrentStructElement = mpGlobalSyncData->mStructParents[ mpGlobalSyncData->mCurrentStructElement ];
}
bool PDFExtOutDevData::SetCurrentStructureElement( sal_Int32 nStructId )
{
    bool bSuccess = false;
    if( o3tl::make_unsigned(nStructId) < mpGlobalSyncData->mStructParents.size() )
    {
        mpGlobalSyncData->mCurrentStructElement = nStructId;
        mpPageSyncData->PushAction( mrOutDev, PDFExtOutDevDataSync::SetCurrentStructureElement );
        mpPageSyncData->mParaInts.push_back( nStructId );
        bSuccess = true;
    }
    return bSuccess;
}
sal_Int32 PDFExtOutDevData::GetCurrentStructureElement() const
{
    return mpGlobalSyncData->mCurrentStructElement;
}
void PDFExtOutDevData::SetStructureAttribute( PDFWriter::StructAttribute eAttr, PDFWriter::StructAttributeValue eVal )
{
    mpPageSyncData->PushAction( mrOutDev, PDFExtOutDevDataSync::SetStructureAttribute );
    mpPageSyncData->mParaStructAttributes.push_back( eAttr );
    mpPageSyncData->mParaStructAttributeValues.push_back( eVal );
}
void PDFExtOutDevData::SetStructureAttributeNumerical( PDFWriter::StructAttribute eAttr, sal_Int32 nValue )
{
    mpPageSyncData->PushAction( mrOutDev, PDFExtOutDevDataSync::SetStructureAttributeNumerical );
    mpPageSyncData->mParaStructAttributes.push_back( eAttr );
    mpPageSyncData->mParaInts.push_back( nValue );
}
void PDFExtOutDevData::SetStructureBoundingBox( const tools::Rectangle& rRect )
{
    mpPageSyncData->PushAction( mrOutDev, PDFExtOutDevDataSync::SetStructureBoundingBox );
    mpPageSyncData->mParaRects.push_back( rRect );
}

void PDFExtOutDevData::SetStructureAnnotIds(::std::vector<sal_Int32> const& rAnnotIds)
{
    mpPageSyncData->PushAction(mrOutDev, PDFExtOutDevDataSync::SetStructureAnnotIds);
    mpPageSyncData->mParaInts.push_back(rAnnotIds.size());
    for (sal_Int32 const id : rAnnotIds)
    {
        mpPageSyncData->mParaInts.push_back(id);
    }
}

void PDFExtOutDevData::SetActualText( const OUString& rText )
{
    mpPageSyncData->PushAction( mrOutDev, PDFExtOutDevDataSync::SetActualText );
    mpPageSyncData->mParaOUStrings.push_back( rText );
}
void PDFExtOutDevData::SetAlternateText( const OUString& rText )
{
    mpPageSyncData->PushAction( mrOutDev, PDFExtOutDevDataSync::SetAlternateText );
    mpPageSyncData->mParaOUStrings.push_back( rText );
}

void PDFExtOutDevData::CreateControl( const PDFWriter::AnyWidget& rControlType )
{
    mpPageSyncData->PushAction( mrOutDev, PDFExtOutDevDataSync::CreateControl );

    std::shared_ptr< PDFWriter::AnyWidget > pClone( rControlType.Clone() );
    mpPageSyncData->mControls.push_back( pClone );
    mpGlobalSyncData->mCurId++;
}

void PDFExtOutDevData::BeginGroup()
{
    mpPageSyncData->PushAction( mrOutDev, PDFExtOutDevDataSync::BeginGroup );
}

void PDFExtOutDevData::EndGroup( const Graphic&     rGraphic,
                                 sal_uInt8          nTransparency,
                                 const tools::Rectangle&   rOutputRect,
                                 const tools::Rectangle&   rVisibleOutputRect )
{
    mpPageSyncData->PushAction( mrOutDev, PDFExtOutDevDataSync::EndGroupGfxLink );
    mpPageSyncData->mGraphics.push_back( rGraphic );
    mpPageSyncData->mParaInts.push_back( nTransparency );
    mpPageSyncData->mParaRects.push_back( rOutputRect );
    mpPageSyncData->mParaRects.push_back( rVisibleOutputRect );
}

// Avoids expensive de-compression and re-compression of large images.
bool PDFExtOutDevData::HasAdequateCompression( const Graphic &rGraphic,
                                               const tools::Rectangle & rOutputRect,
                                               const tools::Rectangle & rVisibleOutputRect ) const
{
    assert(rGraphic.IsGfxLink() &&
           (rGraphic.GetGfxLink().GetType() == GfxLinkType::NativeJpg ||
            rGraphic.GetGfxLink().GetType() == GfxLinkType::NativePng ||
            rGraphic.GetGfxLink().GetType() == GfxLinkType::NativePdf));

    if (rOutputRect != rVisibleOutputRect)
        // rOutputRect is the crop rectangle, re-compress cropped image.
        return false;

    if (mbReduceImageResolution)
        // Reducing resolution was requested, implies that re-compressing is
        // wanted.
        return false;

    auto nSize = rGraphic.GetGfxLink().GetDataSize();
    if (nSize == 0)
        return false;

    GfxLink aLink = rGraphic.GetGfxLink();
    SvMemoryStream aMemoryStream(const_cast<sal_uInt8*>(aLink.GetData()), aLink.GetDataSize(),
                                 StreamMode::READ | StreamMode::WRITE);
    GraphicDescriptor aDescriptor(aMemoryStream, nullptr);
    if (aDescriptor.Detect(true) && aDescriptor.GetNumberOfImageComponents() == 4)
        // 4 means CMYK, which is not handled.
        return false;

    const Size aSize = rGraphic.GetSizePixel();

    // small items better off as PNG anyway
    if ( aSize.Width() < 32 &&
         aSize.Height() < 32 )
        return false;

    if (GetIsLosslessCompression())
        return !GetIsReduceImageResolution();

    // FIXME: ideally we'd also pre-empt the DPI related scaling too.
    sal_Int32 nCurrentRatio = (100 * aSize.Width() * aSize.Height() * 4) /
                               nSize;

    static const struct {
        sal_Int32 mnQuality;
        sal_Int32 mnRatio;
    } aRatios[] = { // minimum tolerable compression ratios
        { 100, 400 }, { 95, 700 }, { 90, 1000 }, { 85, 1200 },
        { 80, 1500 }, { 75, 1700 }
    };
    sal_Int32 nTargetRatio = 10000;
    bool bIsTargetRatioReached = false;
    for (auto & rRatio : aRatios)
    {
        if ( mnCompressionQuality > rRatio.mnQuality )
        {
            bIsTargetRatioReached = true;
            break;
        }
        nTargetRatio = rRatio.mnRatio;
    }

    return ((nCurrentRatio > nTargetRatio) && bIsTargetRatioReached);
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
