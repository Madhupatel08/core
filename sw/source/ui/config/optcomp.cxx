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

#include <optcomp.hxx>

#include <cmdid.h>
#include <docsh.hxx>
#include <uiitems.hxx>
#include <view.hxx>
#include <wrtsh.hxx>

#include <vcl/svapp.hxx>
#include <vcl/weld.hxx>
#include <sfx2/docfilt.hxx>
#include <sfx2/fcontnr.hxx>
#include <IDocumentSettingAccess.hxx>
#include <vector>
#include <svtools/restartdialog.hxx>
#include <comphelper/processfactory.hxx>
#include <officecfg/Office/Compatibility.hxx>
#include <osl/diagnose.h>

using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::document;
using namespace ::com::sun::star::uno;

struct SwCompatibilityOptPage_Impl
{
    std::vector< SvtCompatibilityEntry > m_aList;
};

SwCompatibilityOptPage::SwCompatibilityOptPage(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet& rSet)
    : SfxTabPage(pPage, pController, "modules/swriter/ui/optcompatpage.ui", "OptCompatPage", &rSet)
    , m_pWrtShell(nullptr)
    , m_pImpl(new SwCompatibilityOptPage_Impl)
    , m_nSavedOptions(0)
    , m_xMain(m_xBuilder->weld_frame("compatframe"))
    , m_xFormattingLB(m_xBuilder->weld_combo_box("format"))
    , m_xOptionsLB(m_xBuilder->weld_tree_view("options"))
    , m_xDefaultPB(m_xBuilder->weld_button("default"))
{
    m_xOptionsLB->enable_toggle_buttons(weld::ColumnToggleType::Check);

    int nPos = 0;
    for (int i = static_cast<int>(SvtCompatibilityEntry::Index::Module) + 1;
         i < static_cast<int>(SvtCompatibilityEntry::Index::INVALID) - 1; // omit AddTableLineSpacing
         ++i)
    {
        int nCoptIdx = i - 2; /* Do not consider "Name" & "Module" indexes */

        const OUString sEntry = m_xFormattingLB->get_text(nCoptIdx);
        m_xOptionsLB->append();
        m_xOptionsLB->set_toggle(nPos, TRISTATE_FALSE);
        m_xOptionsLB->set_text(nPos, sEntry, 0);
        ++nPos;
    }

    m_sUserEntry = m_xFormattingLB->get_text(m_xFormattingLB->get_count() - 1);

    m_xFormattingLB->clear();

    InitControls( rSet );

    // set handler
    m_xFormattingLB->connect_changed( LINK( this, SwCompatibilityOptPage, SelectHdl ) );
    m_xDefaultPB->connect_clicked( LINK( this, SwCompatibilityOptPage, UseAsDefaultHdl ) );
}

SwCompatibilityOptPage::~SwCompatibilityOptPage()
{
}

static sal_uInt32 convertBools2Ulong_Impl
(
    bool _bAddSpacing,
    bool _bAddSpacingAtPages,
    bool _bUseOurTabStops,
    bool _bNoExtLeading,
    bool _bUseLineSpacing,
    bool _bAddTableSpacing,
    bool _bAddTableLineSpacing,
    bool _bUseObjPos,
    bool _bUseOurTextWrapping,
    bool _bConsiderWrappingStyle,
    bool _bExpandWordSpace,
    bool _bProtectForm,
    bool _bMsWordCompTrailingBlanks,
    bool bSubtractFlysAnchoredAtFlys,
    bool bEmptyDbFieldHidesPara,
    bool bUseVariableWidthNBSP
)
{
    sal_uInt32 nRet = 0;
    sal_uInt32 nSetBit = 1;

    if ( _bAddSpacing )
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if ( _bAddSpacingAtPages )
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if ( _bUseOurTabStops )
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if ( _bNoExtLeading )
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if ( _bUseLineSpacing )
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if ( _bAddTableSpacing )
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if (_bAddTableLineSpacing)
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if ( _bUseObjPos )
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if ( _bUseOurTextWrapping )
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if ( _bConsiderWrappingStyle )
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if ( _bExpandWordSpace )
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if ( _bProtectForm )
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if ( _bMsWordCompTrailingBlanks )
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if (bSubtractFlysAnchoredAtFlys)
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if (bEmptyDbFieldHidesPara)
        nRet |= nSetBit;
    nSetBit = nSetBit << 1;
    if (bUseVariableWidthNBSP)
        nRet |= nSetBit;

    return nRet;
}

void SwCompatibilityOptPage::InitControls( const SfxItemSet& rSet )
{
    // init objectshell and detect document name
    OUString sDocTitle;
    SfxObjectShell* pObjShell = nullptr;
    if ( const SwPtrItem* pItem = rSet.GetItemIfSet( FN_PARAM_WRTSHELL, false ) )
        m_pWrtShell = static_cast<SwWrtShell*>(pItem->GetValue());
    if ( m_pWrtShell )
    {
        pObjShell = m_pWrtShell->GetView().GetDocShell();
        if ( pObjShell )
            sDocTitle = pObjShell->GetTitle();
    }
    else
    {
        m_xMain->set_sensitive(false);
    }
    const OUString& rText = m_xMain->get_label();
    m_xMain->set_label(rText.replaceAll("%DOCNAME", sDocTitle));

    // loading file formats
    const std::vector< SvtCompatibilityEntry > aList = m_aConfigItem.GetList();

    for ( const SvtCompatibilityEntry& rEntry : aList )
    {
        const OUString sEntryName = rEntry.getValue<OUString>( SvtCompatibilityEntry::Index::Name );
        const bool bIsUserEntry    = ( sEntryName == SvtCompatibilityEntry::USER_ENTRY_NAME );
        const bool bIsDefaultEntry = ( sEntryName == SvtCompatibilityEntry::DEFAULT_ENTRY_NAME );

        m_pImpl->m_aList.push_back( rEntry );

        if ( bIsDefaultEntry )
            continue;

        OUString sNewEntry;
        if ( bIsUserEntry )
            sNewEntry = m_sUserEntry;

        else if ( pObjShell && !sEntryName.isEmpty() )
        {
            SfxFilterContainer* pFacCont = pObjShell->GetFactory().GetFilterContainer();
            std::shared_ptr<const SfxFilter> pFilter = pFacCont->GetFilter4FilterName( sEntryName );
            if ( pFilter )
                sNewEntry = pFilter->GetUIName();
        }

        if ( sNewEntry.isEmpty() )
            sNewEntry = sEntryName;

        sal_uInt32 nOptions = convertBools2Ulong_Impl(
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::AddSpacing ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::AddSpacingAtPages ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::UseOurTabStops ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::NoExtLeading ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::UseLineSpacing ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::AddTableSpacing ),
            rEntry.getValue<bool>(SvtCompatibilityEntry::Index::AddTableLineSpacing),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::UseObjectPositioning ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::UseOurTextWrapping ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::ConsiderWrappingStyle ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::ExpandWordSpace ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::ProtectForm ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::MsWordTrailingBlanks ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::SubtractFlysAnchoredAtFlys ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::EmptyDbFieldHidesPara ),
            rEntry.getValue<bool>( SvtCompatibilityEntry::Index::UseVariableWidthNBSP ) );
        m_xFormattingLB->append(OUString::number(nOptions), sNewEntry);
    }
}

IMPL_LINK_NOARG(SwCompatibilityOptPage, SelectHdl, weld::ComboBox&, void)
{
    sal_uInt32 nOptions = m_xFormattingLB->get_active_id().toUInt32();
    SetCurrentOptions(nOptions);
}

IMPL_LINK_NOARG(SwCompatibilityOptPage, UseAsDefaultHdl, weld::Button&, void)
{
    std::unique_ptr<weld::Builder> xBuilder(Application::CreateBuilder(GetFrameWeld(), "modules/swriter/ui/querydefaultcompatdialog.ui"));
    std::unique_ptr<weld::MessageDialog> xQueryBox(xBuilder->weld_message_dialog("QueryDefaultCompatDialog"));
    if (xQueryBox->run() != RET_YES)
        return;

    auto pItem = std::find_if(m_pImpl->m_aList.begin(), m_pImpl->m_aList.end(),
        [](const SvtCompatibilityEntry& rItem)
        {
            const OUString sEntryName = rItem.getValue<OUString>( SvtCompatibilityEntry::Index::Name );
            const bool bIsDefaultEntry = ( sEntryName == SvtCompatibilityEntry::DEFAULT_ENTRY_NAME );
            return bIsDefaultEntry;
        });
    if (pItem != m_pImpl->m_aList.end())
    {
        const sal_Int32 nCount = m_xOptionsLB->n_children();
        for ( sal_Int32 i = 0; i < nCount; ++i )
        {
            bool bChecked = m_xOptionsLB->get_toggle(i);

            int nCoptIdx = i + 2; /* Consider "Name" & "Module" indexes */
            pItem->setValue<bool>( SvtCompatibilityEntry::Index(nCoptIdx), bChecked );
            if (nCoptIdx == int(SvtCompatibilityEntry::Index::AddTableSpacing))
            {
                bool const isLineSpacing = m_xOptionsLB->get_toggle(i) == TRISTATE_TRUE;
                pItem->setValue<bool>(SvtCompatibilityEntry::Index::AddTableLineSpacing, isLineSpacing);
            }
            else
            {
                assert(m_xOptionsLB->get_toggle(i) != TRISTATE_INDET);
            }
        }
    }

    WriteOptions();
}

void SwCompatibilityOptPage::SetCurrentOptions( sal_uInt32 nOptions )
{
    const int nCount = m_xOptionsLB->n_children();
    OSL_ENSURE( nCount <= 32, "SwCompatibilityOptPage::Reset(): entry overflow" );
    for (int i = 0; i < nCount; ++i)
    {
        bool bChecked = ( ( nOptions & 0x00000001 ) == 0x00000001 );
        TriState value = bChecked ? TRISTATE_TRUE : TRISTATE_FALSE;
        if (i == int(SvtCompatibilityEntry::Index::AddTableSpacing) - 2)
        {   // hack: map 2 bools to 1 tristate
            nOptions = nOptions >> 1;
            if (value == TRISTATE_TRUE
                && (nOptions & 0x00000001) != 0x00000001) // ADD_PARA_LINE_SPACING_TO_TABLE_CELLS
            {
                value = TRISTATE_INDET; // 3 values possible here
            }
        }
        m_xOptionsLB->set_toggle(i, value);
        nOptions = nOptions >> 1;
    }
}

sal_uInt32 SwCompatibilityOptPage::GetDocumentOptions() const
{
    sal_uInt32 nRet = 0;
    if ( m_pWrtShell )
    {
        const IDocumentSettingAccess& rIDocumentSettingAccess = m_pWrtShell->getIDocumentSettingAccess();
        nRet = convertBools2Ulong_Impl(
            rIDocumentSettingAccess.get( DocumentSettingId::PARA_SPACE_MAX ),
            rIDocumentSettingAccess.get( DocumentSettingId::PARA_SPACE_MAX_AT_PAGES ),
            !rIDocumentSettingAccess.get( DocumentSettingId::TAB_COMPAT ),
            !rIDocumentSettingAccess.get( DocumentSettingId::ADD_EXT_LEADING ),
            rIDocumentSettingAccess.get( DocumentSettingId::OLD_LINE_SPACING ),
            rIDocumentSettingAccess.get( DocumentSettingId::ADD_PARA_SPACING_TO_TABLE_CELLS ),
            rIDocumentSettingAccess.get( DocumentSettingId::ADD_PARA_LINE_SPACING_TO_TABLE_CELLS ),
            rIDocumentSettingAccess.get( DocumentSettingId::USE_FORMER_OBJECT_POS ),
            rIDocumentSettingAccess.get( DocumentSettingId::USE_FORMER_TEXT_WRAPPING ),
            rIDocumentSettingAccess.get( DocumentSettingId::CONSIDER_WRAP_ON_OBJECT_POSITION ),
            !rIDocumentSettingAccess.get( DocumentSettingId::DO_NOT_JUSTIFY_LINES_WITH_MANUAL_BREAK ),
            rIDocumentSettingAccess.get( DocumentSettingId::PROTECT_FORM ),
            rIDocumentSettingAccess.get( DocumentSettingId::MS_WORD_COMP_TRAILING_BLANKS ),
            rIDocumentSettingAccess.get( DocumentSettingId::SUBTRACT_FLYS ),
            rIDocumentSettingAccess.get( DocumentSettingId::EMPTY_DB_FIELD_HIDES_PARA ),
            rIDocumentSettingAccess.get( DocumentSettingId::USE_VARIABLE_WIDTH_NBSP ) );
    }
    return nRet;
}

void SwCompatibilityOptPage::WriteOptions()
{
    m_aConfigItem.Clear();
    for ( const auto& rItem : m_pImpl->m_aList )
        m_aConfigItem.AppendItem(rItem);
}

std::unique_ptr<SfxTabPage> SwCompatibilityOptPage::Create(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet* rAttrSet)
{
    return std::make_unique<SwCompatibilityOptPage>(pPage, pController, *rAttrSet);
}

OUString SwCompatibilityOptPage::GetAllStrings()
{
    OUString sAllStrings = m_xBuilder->weld_label(u"label11")->get_label() + " ";

    sAllStrings += m_xDefaultPB->get_label() + " ";

    return sAllStrings.replaceAll("_", "");
}

bool SwCompatibilityOptPage::FillItemSet( SfxItemSet*  )
{
    bool bModified = false;
    if ( m_pWrtShell )
    {
        sal_uInt32 nSavedOptions = m_nSavedOptions;
        const int nCount = m_xOptionsLB->n_children();
        OSL_ENSURE( nCount <= 32, "SwCompatibilityOptPage::Reset(): entry overflow" );

        for (int i = 0; i < nCount; ++i)
        {
            TriState const current = m_xOptionsLB->get_toggle(i);
            TriState saved = ((nSavedOptions & 0x00000001) == 0x00000001) ? TRISTATE_TRUE : TRISTATE_FALSE;
            if (i == int(SvtCompatibilityEntry::Index::AddTableSpacing) - 2)
            {   // hack: map 2 bools to 1 tristate
                nSavedOptions = nSavedOptions >> 1;
                if (saved == TRISTATE_TRUE
                    && ((nSavedOptions & 0x00000001) != 0x00000001))
                {
                    saved = TRISTATE_INDET;
                }
            }
            if (current != saved)
            {
                bool const bChecked(current != TRISTATE_FALSE);
                assert(current != TRISTATE_INDET); // can't *change* it to that
                int nCoptIdx = i + 2; /* Consider "Name" & "Module" indexes */
                switch ( SvtCompatibilityEntry::Index(nCoptIdx) )
                {
                    case SvtCompatibilityEntry::Index::AddSpacing:
                        m_pWrtShell->SetParaSpaceMax( bChecked );
                        break;

                    case SvtCompatibilityEntry::Index::AddSpacingAtPages:
                        m_pWrtShell->SetParaSpaceMaxAtPages( bChecked );
                        break;

                    case SvtCompatibilityEntry::Index::UseOurTabStops:
                        m_pWrtShell->SetTabCompat( !bChecked );
                        break;

                    case SvtCompatibilityEntry::Index::NoExtLeading:
                        m_pWrtShell->SetAddExtLeading( !bChecked );
                        break;

                    case SvtCompatibilityEntry::Index::UseLineSpacing:
                        m_pWrtShell->SetUseFormerLineSpacing( bChecked );
                        break;

                    case SvtCompatibilityEntry::Index::AddTableSpacing:
                        m_pWrtShell->SetAddParaSpacingToTableCells( bChecked );
                        break;

                    case SvtCompatibilityEntry::Index::UseObjectPositioning:
                        m_pWrtShell->SetUseFormerObjectPositioning( bChecked );
                        break;

                    case SvtCompatibilityEntry::Index::UseOurTextWrapping:
                        m_pWrtShell->SetUseFormerTextWrapping( bChecked );
                        break;

                    case SvtCompatibilityEntry::Index::ConsiderWrappingStyle:
                        m_pWrtShell->SetConsiderWrapOnObjPos( bChecked );
                        break;

                    case SvtCompatibilityEntry::Index::ExpandWordSpace:
                        m_pWrtShell->SetDoNotJustifyLinesWithManualBreak( !bChecked );
                        break;

                    case SvtCompatibilityEntry::Index::ProtectForm:
                        m_pWrtShell->SetProtectForm( bChecked );
                        break;

                    case SvtCompatibilityEntry::Index::MsWordTrailingBlanks:
                        m_pWrtShell->SetMsWordCompTrailingBlanks( bChecked );
                        break;

                    case SvtCompatibilityEntry::Index::SubtractFlysAnchoredAtFlys:
                        m_pWrtShell->SetSubtractFlysAnchoredAtFlys(bChecked);
                        break;

                    case SvtCompatibilityEntry::Index::EmptyDbFieldHidesPara:
                        m_pWrtShell->SetEmptyDbFieldHidesPara(bChecked);
                        break;

                    case SvtCompatibilityEntry::Index::UseVariableWidthNBSP:
                        m_pWrtShell->GetDoc()->getIDocumentSettingAccess()
                            .set(DocumentSettingId::USE_VARIABLE_WIDTH_NBSP, bChecked);
                        break;

                    default:
                        break;
                }
                bModified = true;
            }

            nSavedOptions = nSavedOptions >> 1;
        }
    }

    if ( bModified )
        WriteOptions();

    return bModified;
}

void SwCompatibilityOptPage::Reset( const SfxItemSet*  )
{
    m_xOptionsLB->select(0);

    sal_uInt32 nOptions = GetDocumentOptions();
    SetCurrentOptions( nOptions );
    m_nSavedOptions = nOptions;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
