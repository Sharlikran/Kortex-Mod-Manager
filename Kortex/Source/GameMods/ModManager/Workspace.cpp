#include "stdafx.h"
#include <Kortex/Application.hpp>
#include <Kortex/ApplicationOptions.hpp>
#include <Kortex/ModManager.hpp>
#include <Kortex/ModImporter.hpp>
#include <Kortex/ModStatistics.hpp>
#include <Kortex/ModTagManager.hpp>
#include <Kortex/ProgramManager.hpp>
#include <Kortex/InstallWizard.hpp>
#include <Kortex/VirtualGameFolder.hpp>
#include <Kortex/GameInstance.hpp>
#include <Kortex/NetworkManager.hpp>

#include "GameInstance/ProfileEditor.h"
#include "Workspace.h"
#include "DisplayModel.h"
#include "NewModDialog.h"
#include "VirtualFileSystem/VirtualFSEvent.h"
#include "UI/TextEditDialog.h"
#include "Utility/KOperationWithProgress.h"
#include "Utility/KAux.h"
#include "Utility/UI.h"
#include "Utility/MenuSeparator.h"
#include <KxFramework/KxFile.h>
#include <KxFramework/KxShell.h>
#include <KxFramework/KxLabel.h>
#include <KxFramework/KxSearchBox.h>
#include <KxFramework/KxTaskDialog.h>
#include <KxFramework/KxFileBrowseDialog.h>
#include <KxFramework/KxDualProgressDialog.h>
#include <KxFramework/KxFileOperationEvent.h>
#include <wx/colordlg.h>

namespace Kortex::Application::OName
{
	KortexDefOption(Splitter);
	KortexDefOption(RightPane);

	KortexDefOption(ShowPriorityGroups);
	KortexDefOption(ShowNotInstalledMods);
	KortexDefOption(BoldPriorityGroupLabels);
	KortexDefOption(PriorityGroupLabelAlignment);
	KortexDefOption(DisplayMode);
	KortexDefOption(ImageResizeMode);
}

namespace
{
	using namespace Kortex;
	using namespace Kortex::ModManager;
	using namespace Kortex::Application;

	enum class DisplayModeMenuID
	{
		Connector,
		Manager,
		ShowPriorityGroups,
		BoldPriorityGroupLabels,
		ShowNotInstalledMods,

		PriorityGroupLabelAlignment_Left,
		PriorityGroupLabelAlignment_Right,
		PriorityGroupLabelAlignment_Center,
	};
	enum class ToolsMenuID
	{
		Statistics = KxID_HIGHEST + 1,
		ExportModList,
	};
	enum class ContextMenuID
	{
		BeginIndex = KxID_HIGHEST,

		ModOpenLocation,
		ModChangeLocation,
		ModRevertLocation,

		ModInstall,
		ModUninstall,
		ModErase,

		ModEditDescription,
		ModEditTags,
		ModEditSources,
		ModChangeID,
		ModProperties,

		ColorAssign,
		ColorReset,
	};

	template<class Functor> bool DoForAllSelectedItems(const IGameMod::RefVector& selectedMods, Functor&& func)
	{
		if (!selectedMods.empty())
		{
			bool result = false;
			for (IGameMod* gameMod: selectedMods)
			{
				if (!func(*gameMod))
				{
					break;
				}
				result = true;
			}
			return result;
		}
		return false;
	}

	auto GetDisplayModelOptions()
	{
		return Application::GetAInstanceOptionOf<IModManager>(OName::Workspace, OName::DisplayModel);
	}
	auto GetSplitterOptions()
	{
		return Application::GetAInstanceOptionOf<IModManager>(OName::Workspace, OName::Splitter);
	}
	auto GetRightPaneOptions()
	{
		return Application::GetAInstanceOptionOf<IModManager>(OName::Workspace, OName::RightPane);
	}
}

namespace Kortex::ModManager
{
	void WorkspaceContainer::Create(wxWindow* parent)
	{
		m_BookCtrl = new KxAuiNotebook(parent, KxID_NONE, KxAuiNotebook::DefaultStyle|wxAUI_NB_TAB_MOVE);
		m_BookCtrl->SetImageList(&ImageProvider::GetImageList());

		m_BookCtrl->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGING, &WorkspaceContainer::OnPageOpening, this);
		m_BookCtrl->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &WorkspaceContainer::OnPageOpened, this);
	}
	void WorkspaceContainer::OnPageOpening(wxAuiNotebookEvent& event)
	{
		if (IWorkspace* nextWorkspace = GetWorkspaceByIndex(event.GetSelection()))
		{
			if (nextWorkspace->SwitchHere())
			{
				event.Allow();
			}
			else
			{
				event.Veto();
			}
		}
		event.Skip();
	}
	void WorkspaceContainer::OnPageOpened(wxAuiNotebookEvent& event)
	{
		IWorkspace* workspace = GetWorkspaceByIndex(event.GetSelection());
		if (workspace && workspace->IsCreated())
		{
			event.Allow();
		}
		else
		{
			event.Veto();
		}
		event.Skip();
	}

	IWorkspaceContainer* WorkspaceContainer::GetParentContainer()
	{
		return &IMainWindow::GetInstance()->GetWorkspaceContainer();
	}
}

namespace Kortex::ModManager
{
	bool Workspace::OnCreateWorkspace()
	{
		m_MainSizer = new wxBoxSizer(wxVERTICAL);
		SetSizer(m_MainSizer);

		m_SplitterLeftRight = new KxSplitterWindow(this, KxID_NONE);
		m_SplitterLeftRight->SetName("Horizontal");
		m_SplitterLeftRight->SetMinimumPaneSize(250);
		m_MainSizer->Add(m_SplitterLeftRight, 1, wxEXPAND);
		IThemeManager::GetActive().ProcessWindow(m_SplitterLeftRight, true);

		// Mods Panel
		m_ModsPaneSizer = new wxBoxSizer(wxVERTICAL);
		m_ModsPane = new KxPanel(m_SplitterLeftRight, KxID_NONE);
		m_ModsPane->SetSizer(m_ModsPaneSizer);
		m_ModsPane->SetBackgroundColour(GetBackgroundColour());

		CreateModsView();
		CreateToolBar();
		m_ModsPaneSizer->Add(m_ModsToolBar, 0, wxEXPAND);
		m_ModsPaneSizer->Add(m_DisplayModel->GetView(), 1, wxEXPAND|wxTOP, KLC_VERTICAL_SPACING);

		m_WorkspaceContainer.Create(m_SplitterLeftRight);
		CreateControls();

		ReloadView();
		UpdateModListContent();

		m_BroadcastReciever.Bind(VirtualFSEvent::EvtMainToggled, &Workspace::OnMainFSToggled, this);
		m_BroadcastReciever.Bind(ProfileEvent::EvtSelected, &Workspace::OnProfileSelected, this);

		m_BroadcastReciever.Bind(ModEvent::EvtInstalled, &Workspace::OnUpdateModLayoutNeeded, this);
		m_BroadcastReciever.Bind(ModEvent::EvtUninstalled, &Workspace::OnUpdateModLayoutNeeded, this);
		m_BroadcastReciever.Bind(ModEvent::EvtFilesChanged, &Workspace::OnUpdateModLayoutNeeded, this);
		return true;
	}
	bool Workspace::OnOpenWorkspace()
	{
		if (!OpenedOnce())
		{
			IMainWindow::GetInstance()->InitializeWorkspaces();

			m_SplitterLeftRight->SplitVertically(m_ModsPane, &m_WorkspaceContainer.GetWindow());
			GetSplitterOptions().LoadSplitterLayout(m_SplitterLeftRight);
			//m_WorkspaceContainer.GetAuiManager().LoadPerspective(GetRightPaneOptions().GetValue(), false);

			auto displayModelOptions = GetDisplayModelOptions();
			displayModelOptions.LoadDataViewLayout(m_DisplayModel->GetView());

			m_DisplayModel->LoadView();
		}
		m_DisplayModel->UpdateUI();
		return true;
	}
	bool Workspace::OnCloseWorkspace()
	{
		IMainWindow::GetInstance()->ClearStatus();
		return true;
	}
	void Workspace::OnReloadWorkspace()
	{
		ClearControls();

		m_DisplayModel->LoadView();
		ProcessSelection();
		UpdateModListContent();
	}

	Workspace::~Workspace()
	{
		if (IsCreated())
		{
			auto options = GetDisplayModelOptions();
			options.SaveDataViewLayout(m_DisplayModel->GetView());

			options.SetAttribute(OName::DisplayMode, (int)m_DisplayModel->GetDisplayMode());
			options.SetAttribute(OName::ShowPriorityGroups, m_DisplayModel->ShouldShowPriorityGroups());
			options.SetAttribute(OName::ShowNotInstalledMods, m_DisplayModel->ShouldShowNotInstalledMods());
			options.SetAttribute(OName::BoldPriorityGroupLabels, m_DisplayModel->IsBoldPriorityGroupLabels());
			options.SetAttribute(OName::PriorityGroupLabelAlignment, (int)m_DisplayModel->GetPriorityGroupLabelAlignment());

			GetSplitterOptions().SaveSplitterLayout(m_SplitterLeftRight);
			//GetRightPaneOptions().SetValue(m_WorkspaceContainer.GetAuiManager().SavePerspective(), KxXMLDocument::AsCDATA::Always);
		}
	}

	void Workspace::CreateToolBar()
	{
		m_ModsToolBar = new KxAuiToolBar(m_ModsPane, KxID_NONE, wxAUI_TB_HORZ_TEXT|wxAUI_TB_PLAIN_BACKGROUND);
		m_ModsToolBar->SetBackgroundColour(GetBackgroundColour());
		m_ModsToolBar->SetMargins(0, 1, 0, 0);

		m_ToolBar_Profiles = new KxComboBox(m_ModsToolBar, KxID_NONE);
		m_ToolBar_Profiles->Bind(wxEVT_COMBOBOX, &Workspace::OnSelectProfile, this);

		m_ModsToolBar->AddLabel(KTr("ModManager.Profile") + ':');
		m_ModsToolBar->AddControl(m_ToolBar_Profiles)->SetProportion(1);

		m_ToolBar_EditProfiles = Utility::UI::CreateToolBarButton(m_ModsToolBar, wxEmptyString, ImageResourceID::Gear);
		m_ToolBar_EditProfiles->SetShortHelp(KTr("ModManager.Profile.Configure"));
		m_ToolBar_EditProfiles->Bind(KxEVT_AUI_TOOLBAR_CLICK, &Workspace::OnShowProfileEditor, this);
		m_ModsToolBar->AddSeparator();

		m_ToolBar_AddMod = Utility::UI::CreateToolBarButton(m_ModsToolBar, KTr(KxID_ADD), ImageResourceID::PlusSmall);
	
		m_ToolBar_ChangeDisplayMode = Utility::UI::CreateToolBarButton(m_ModsToolBar, KTr("ModManager.DisplayMode.Caption"), ImageResourceID::ProjectionScreen);
		m_ToolBar_ChangeDisplayMode->Bind(KxEVT_AUI_TOOLBAR_CLICK, &Workspace::OnDisplayModeMenu, this);
	
		m_ToolBar_Tools = Utility::UI::CreateToolBarButton(m_ModsToolBar, wxEmptyString, ImageResourceID::WrenchScrewdriver);
		m_ToolBar_Tools->SetShortHelp(KTr("ModManager.Tools"));
		m_ToolBar_Tools->Bind(KxEVT_AUI_TOOLBAR_CLICK, &Workspace::OnToolsMenu, this);

		m_SearchBox = new KxSearchBox(m_ModsToolBar, KxID_NONE);
		m_SearchBox->SetName("Workspace search box");
		m_SearchBox->Bind(wxEVT_SEARCHCTRL_SEARCH_BTN, &Workspace::OnModSerach, this);
		m_SearchBox->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, &Workspace::OnModSerach, this);

		KxMenu* searchMenu = new KxMenu();
		searchMenu->Bind(KxEVT_MENU_SELECT, &Workspace::OnModSearchColumnsChanged, this);
		m_DisplayModel->CreateSearchColumnsMenu(*searchMenu);
		m_SearchBox->SetMenu(searchMenu);

		m_ModsToolBar->AddControl(m_SearchBox);

		m_ModsToolBar->Realize();

		CreateDisplayModeMenu();
		CreateAddModMenu();
		CreateToolsMenu();
	}
	void Workspace::CreateDisplayModeMenu()
	{
		m_ToolBar_DisplayModeMenu = new KxMenu();
		m_ToolBar_ChangeDisplayMode->AssignDropdownMenu(m_ToolBar_DisplayModeMenu);
		m_ToolBar_ChangeDisplayMode->SetOptionEnabled(KxAUI_TBITEM_OPTION_LCLICK_MENU);

		{
			KxMenuItem* item = m_ToolBar_DisplayModeMenu->Add(new KxMenuItem((int)DisplayModeMenuID::Connector, KTr("ModManager.DisplayMode.Connector"), wxEmptyString, wxITEM_RADIO));
			item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::PlugDisconnect));
			item->Check(m_DisplayModel->GetDisplayMode() == DisplayModelType::Connector);
		}
		{
			KxMenuItem* item = m_ToolBar_DisplayModeMenu->Add(new KxMenuItem((int)DisplayModeMenuID::Manager, KTr("ModManager.DisplayMode.Log"), wxEmptyString, wxITEM_RADIO));
			item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::Category));
			item->Check(m_DisplayModel->GetDisplayMode() == DisplayModelType::Manager);
		}
		m_ToolBar_DisplayModeMenu->AddSeparator();

		{
			KxMenuItem* item = m_ToolBar_DisplayModeMenu->Add(new KxMenuItem((int)DisplayModeMenuID::ShowPriorityGroups, KTr("ModManager.DisplayMode.ShowPriorityGroups"), wxEmptyString, wxITEM_CHECK));
			item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::Folders));
			item->Check(m_DisplayModel->ShouldShowPriorityGroups());
		}
		{
			KxMenuItem* item = m_ToolBar_DisplayModeMenu->Add(new KxMenuItem((int)DisplayModeMenuID::ShowNotInstalledMods, KTr("ModManager.DisplayMode.ShowNotInstalledMods"), wxEmptyString, wxITEM_CHECK));
			item->Check(m_DisplayModel->ShouldShowNotInstalledMods());
		}
		{
			KxMenuItem* item = m_ToolBar_DisplayModeMenu->Add(new KxMenuItem((int)DisplayModeMenuID::BoldPriorityGroupLabels, KTr("ModManager.DisplayMode.BoldPriorityGroupLabels"), wxEmptyString, wxITEM_CHECK));
			item->Check(m_DisplayModel->IsBoldPriorityGroupLabels());
		}
		m_ToolBar_DisplayModeMenu->AddSeparator();

		{
			KxMenu* alignmnetMenu = new KxMenu();
			m_ToolBar_DisplayModeMenu->Add(alignmnetMenu, KTr("ModManager.DisplayMode.PriorityGroupLabelsAlignment"));

			using PriorityGroupLabelAlignment = DisplayModel::PriorityGroupLabelAlignment;
			auto AddOption = [this, alignmnetMenu](DisplayModeMenuID id, PriorityGroupLabelAlignment type, KxStandardID trId)
			{
				KxMenuItem* item = alignmnetMenu->Add(new KxMenuItem((int)id, KTr(trId), wxEmptyString, wxITEM_RADIO));
				item->Check(m_DisplayModel->GetPriorityGroupLabelAlignment() == type);
				return item;
			};
			AddOption(DisplayModeMenuID::PriorityGroupLabelAlignment_Left, PriorityGroupLabelAlignment::Left, KxID_JUSTIFY_LEFT);
			AddOption(DisplayModeMenuID::PriorityGroupLabelAlignment_Center, PriorityGroupLabelAlignment::Center, KxID_JUSTIFY_CENTER);
			AddOption(DisplayModeMenuID::PriorityGroupLabelAlignment_Right, PriorityGroupLabelAlignment::Right, KxID_JUSTIFY_RIGHT);
		}
	}
	void Workspace::CreateAddModMenu()
	{
		m_ToolBar_AddModMenu = new KxMenu();
		m_ToolBar_AddMod->AssignDropdownMenu(m_ToolBar_AddModMenu);
		m_ToolBar_AddMod->SetOptionEnabled(KxAUI_TBITEM_OPTION_LCLICK_MENU);
		KxMenuItem* item = nullptr;

		item = m_ToolBar_AddModMenu->Add(new KxMenuItem(KTr("ModManager.NewMod.NewEmptyMod")));
		item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::FolderPlus));
		item->Bind(KxEVT_MENU_SELECT, &Workspace::OnAddMod_Empty, this);

		item = m_ToolBar_AddModMenu->Add(new KxMenuItem(KTr("ModManager.NewMod.NewModFromFolder")));
		item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::FolderArrow));
		item->Bind(KxEVT_MENU_SELECT, &Workspace::OnAddMod_FromFolder, this);
	
		item = m_ToolBar_AddModMenu->Add(new KxMenuItem(KTr("ModManager.NewMod.InstallPackage")));
		item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::BoxSearchResult));
		item->Bind(KxEVT_MENU_SELECT, &Workspace::OnAddMod_InstallPackage, this);

		m_ToolBar_AddModMenu->AddSeparator();

		item = m_ToolBar_AddModMenu->Add(new KxMenuItem(KTrf("ModManager.NewMod.ImportFrom", "Mod Organizer")));
		item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::MO2));
		item->Bind(KxEVT_MENU_SELECT, [this](KxMenuEvent& event)
		{
			IModImporter::PerformImport(IModImporter::Type::ModOrganizer, this);
		});

		item = m_ToolBar_AddModMenu->Add(new KxMenuItem(KTrf("ModManager.NewMod.ImportFrom", "Nexus Mod Manager")));
		item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::ModNetwork_Nexus));
		item->Bind(KxEVT_MENU_SELECT, [this](KxMenuEvent& event)
		{
			IModImporter::PerformImport(IModImporter::Type::NexusModManager, this);
		});
	}
	void Workspace::CreateToolsMenu()
	{
		KxMenu* menu = new KxMenu();
		m_ToolBar_Tools->AssignDropdownMenu(menu);
		m_ToolBar_Tools->SetOptionEnabled(KxAUI_TBITEM_OPTION_LCLICK_MENU);
		KxMenuItem* item = nullptr;

		item = menu->Add(new KxMenuItem((int)ToolsMenuID::Statistics, KTr("ModManager.Statistics")));
		item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::Chart));

		item = menu->Add(new KxMenuItem((int)ToolsMenuID::ExportModList, KTr("Generic.Export")));
		item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::Disk));
	}

	void Workspace::CreateModsView()
	{
		const auto options = GetDisplayModelOptions();

		m_DisplayModel = new DisplayModel();
		m_DisplayModel->ShowPriorityGroups(options.GetAttributeBool(OName::ShowPriorityGroups));
		m_DisplayModel->ShowNotInstalledMods(options.GetAttributeBool(OName::ShowNotInstalledMods));
		m_DisplayModel->SetBoldPriorityGroupLabels(options.GetAttributeBool(OName::BoldPriorityGroupLabels));
		m_DisplayModel->SetPriorityGroupLabelAlignment((DisplayModel::PriorityGroupLabelAlignment)options.GetAttributeInt(OName::PriorityGroupLabelAlignment));
	
		m_DisplayModel->CreateView(m_ModsPane);
		m_DisplayModel->SetDisplayMode((DisplayModelType)options.GetAttributeInt(OName::DisplayMode));
	}
	void Workspace::CreateControls()
	{
		wxBoxSizer* controlsSizer = new wxBoxSizer(wxHORIZONTAL);
		m_MainSizer->Add(controlsSizer, 0, wxEXPAND|wxALIGN_RIGHT|wxTOP, KLC_VERTICAL_SPACING);

		m_ActivateButton = new KxButton(this, KxID_NONE, KTr("ModManager.VFS.Activate"));
		m_ActivateButton->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::TickCircleFrameEmpty));
		m_ActivateButton->Bind(wxEVT_BUTTON, &Workspace::OnMountButton, this);

		controlsSizer->Add(m_ActivateButton);
	}

	wxString Workspace::GetID() const
	{
		return wxS("ModManager::Workspace");
	}
	wxString Workspace::GetName() const
	{
		return KTr("ModManager.Name");
	}
	IWorkspaceContainer* Workspace::GetPreferredContainer() const
	{
		return &IMainWindow::GetInstance()->GetWorkspaceContainer();
	}

	void Workspace::OnMountButton(wxCommandEvent& event)
	{
		IVirtualFileSystem& vfs = IModManager::GetInstance()->GetFileSystem();
		if (vfs.IsEnabled())
		{
			vfs.Disable();
		}
		else
		{
			vfs.Enable();
		}
	}
	bool Workspace::ShowChangeModIDDialog(IGameMod& mod)
	{
		wxString newID;
		const wxString oldID = mod.GetID();

		KxTextBoxDialog dialog(this, KxID_NONE, KTr("ModManager.Menu.ChangeID"), wxDefaultPosition, wxDefaultSize, KxBTN_OK|KxBTN_CANCEL);
		dialog.SetValue(oldID);
		dialog.Bind(KxEVT_STDDIALOG_BUTTON, [this, &mod, &dialog, &newID](wxNotifyEvent& event)
		{
			if (event.GetId() == KxID_OK)
			{
				newID = dialog.GetValue();
				if (newID.IsEmpty())
				{
					KxTaskDialog(&dialog, KxID_NONE, KTr("InstallWizard.ChangeID.Invalid"), wxEmptyString, KxBTN_OK, KxICON_WARNING).ShowModal();
					event.Veto();
				}
				else if (const IGameMod* existingMod = IModManager::GetInstance()->FindModByID(newID))
				{
					if (existingMod != &mod)
					{
						KxTaskDialog(&dialog, KxID_NONE, KTrf("InstallWizard.ChangeID.Used", existingMod->GetName()), wxEmptyString, KxBTN_OK, KxICON_WARNING).ShowModal();
						event.Veto();
					}
				}
			}
		});

		if (dialog.ShowModal() == KxID_OK && newID != oldID)
		{
			const bool nameIsSame = mod.GetName() == oldID;
			if (IModManager::GetInstance()->ChangeModID(mod, newID))
			{
				if (nameIsSame)
				{
					mod.SetName(newID);
				}
				return true;
			}
		}
		return false;
	}

	void Workspace::ProcessSelectProfile(const wxString& newProfileID)
	{
		if (!newProfileID.IsEmpty())
		{
			IGameInstance* instance = IGameInstance::GetActive();
			IGameProfile* profile = IGameInstance::GetActiveProfile();

			if (!IGameInstance::IsActiveProfileID(newProfileID))
			{
				profile->SyncWithCurrentState();
				profile->SaveConfig();

				IGameProfile* newProfile = instance->GetProfile(newProfileID);
				if (newProfile)
				{
					instance->ChangeProfileTo(*newProfile);
				}
			}
		}
	}
	void Workspace::OnSelectProfile(wxCommandEvent& event)
	{
		ProcessSelectProfile(m_ToolBar_Profiles->GetString(event.GetSelection()));
		m_DisplayModel->GetView()->SetFocus();
	}
	void Workspace::OnShowProfileEditor(KxAuiToolBarEvent& event)
	{
		// Save current
		IGameProfile* profile = IGameInstance::GetActiveProfile();
		profile->SyncWithCurrentState();
		profile->SaveConfig();

		Kortex::ProfileEditor::Dialog dialog(this);
		dialog.ShowModal();
		if (dialog.IsModified())
		{
			ProcessSelectProfile(dialog.GetNewProfile());
			UpdateModListContent();
		}
	}
	void Workspace::OnUpdateModLayoutNeeded(ModEvent& event)
	{
		ScheduleReload();
	}

	void Workspace::OnDisplayModeMenu(KxAuiToolBarEvent& event)
	{
		wxWindowID id = m_ToolBar_ChangeDisplayMode->ShowDropdownMenu();
		if (id != KxID_NONE)
		{
			switch ((DisplayModeMenuID)id)
			{
				case DisplayModeMenuID::Connector:
				{
					m_DisplayModel->SetDisplayMode(DisplayModelType::Connector);
					m_DisplayModel->LoadView();
					ProcessSelection();
					break;
				}
				case DisplayModeMenuID::Manager:
				{
					m_DisplayModel->SetDisplayMode(DisplayModelType::Manager);
					m_DisplayModel->LoadView();
					ProcessSelection();
					break;
				}
				case DisplayModeMenuID::ShowPriorityGroups:
				{
					m_DisplayModel->ShowPriorityGroups(!m_DisplayModel->ShouldShowPriorityGroups());
					m_DisplayModel->LoadView();
					ProcessSelection();
					break;
				}
				case DisplayModeMenuID::ShowNotInstalledMods:
				{
					m_DisplayModel->ShowNotInstalledMods(!m_DisplayModel->ShouldShowNotInstalledMods());
					m_DisplayModel->LoadView();
					ProcessSelection();
					break;
				}
				case DisplayModeMenuID::BoldPriorityGroupLabels:
				{
					m_DisplayModel->SetBoldPriorityGroupLabels(!m_DisplayModel->IsBoldPriorityGroupLabels());
					m_DisplayModel->UpdateUI();
					break;
				}

				using PriorityGroupLabelAlignment = DisplayModel::PriorityGroupLabelAlignment;
				case DisplayModeMenuID::PriorityGroupLabelAlignment_Left:
				{
					m_DisplayModel->SetPriorityGroupLabelAlignment(PriorityGroupLabelAlignment::Left);
					m_DisplayModel->UpdateUI();
					break;
				}
				case DisplayModeMenuID::PriorityGroupLabelAlignment_Right:
				{
					m_DisplayModel->SetPriorityGroupLabelAlignment(PriorityGroupLabelAlignment::Right);
					m_DisplayModel->UpdateUI();
					break;
				}
				case DisplayModeMenuID::PriorityGroupLabelAlignment_Center:
				{
					m_DisplayModel->SetPriorityGroupLabelAlignment(PriorityGroupLabelAlignment::Center);
					m_DisplayModel->UpdateUI();
					break;
				}
			};
		}
	}
	void Workspace::OnToolsMenu(KxAuiToolBarEvent& event)
	{
		switch ((ToolsMenuID)m_ToolBar_Tools->ShowDropdownMenu())
		{
			case ToolsMenuID::Statistics:
			{
				ModStatistics::Dialog dialog(this);
				dialog.ShowModal();
				break;
			}
			case ToolsMenuID::ExportModList:
			{
				KxFileBrowseDialog dialog(this, KxID_NONE, KxFBD_SAVE);
				dialog.AddFilter("*.html", KTr("FileFilter.HTML"));
				dialog.SetDefaultExtension("html");
				dialog.SetFileName(IGameInstance::GetActive()->GetGameID() + wxS(" - ") + IGameInstance::GetActive()->GetInstanceID());

				if (dialog.ShowModal() == KxID_OK)
				{
					IModManager::GetInstance()->ExportModList(dialog.GetResult());
				}
				break;
			}
		};
	}
	void Workspace::OnMainFSToggled(VirtualFSEvent& event)
	{
		m_ToolBar_Profiles->Enable(!event.IsActivated());
		m_ToolBar_EditProfiles->SetEnabled(!event.IsActivated());

		wxWindowUpdateLocker lock(m_ActivateButton);
		if (event.IsActivated())
		{
			m_ActivateButton->SetLabel(KTr("ModManager.VFS.Deactivate"));
			m_ActivateButton->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::InformationFrameEmpty));
		}
		else
		{
			m_ActivateButton->SetLabel(KTr("ModManager.VFS.Activate"));
			m_ActivateButton->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::TickCircleFrameEmpty));
		}
		m_DisplayModel->UpdateUI();
	}
	void Workspace::OnProfileSelected(ProfileEvent& event)
	{
		ReloadView();
	}

	void Workspace::OnAddMod_Empty(KxMenuEvent& event)
	{
		NewModDialog dialog(this);
		if (dialog.ShowModal() == KxID_OK)
		{
			IModManager::GetInstance()->InstallEmptyMod(dialog.GetFolderName());
		}
	}
	void Workspace::OnAddMod_FromFolder(KxMenuEvent& event)
	{
		NewModFromFolderDialog dialog(this);
		if (dialog.ShowModal() == KxID_OK)
		{
			IModManager::GetInstance()->InstallModFromFolder(dialog.GetFolderPath(), dialog.GetFolderName(), dialog.ShouldCreateAsLinkedMod());
		}
	}
	void Workspace::OnAddMod_InstallPackage(KxMenuEvent& event)
	{
		KxFileBrowseDialog dialog(this, KxID_NONE, KxFBD_OPEN);
		dialog.AddFilter("*.kmp;*.smi;*.7z;*.zip;*.fomod", KTr("FileFilter.AllSupportedFormats"));
		dialog.AddFilter("*.kmp", KTr("FileFilter.ModPackage"));
		dialog.AddFilter("*", KTr("FileFilter.AllFiles"));
		if (dialog.ShowModal() == KxID_OK)
		{
			IModManager::GetInstance()->InstallModFromPackage(dialog.GetResult());
		}
	}

	void Workspace::InstallMod(IGameMod& mod)
	{
		new InstallWizard::WizardDialog(this, mod.GetPackageFile());
	}
	void Workspace::UninstallMod(IGameMod& mod, bool eraseLog)
	{
		KxTaskDialog dialog(this, KxID_NONE, wxEmptyString, KTr("ModManager.RemoveMod.Message"), KxBTN_YES|KxBTN_NO, KxICON_WARNING);
		if (mod.IsInstalled())
		{
			dialog.SetCaption(eraseLog ? KTrf("ModManager.RemoveMod.CaptionUninstallAndErase", mod.GetName()) : KTrf("ModManager.RemoveMod.CaptionUninstall", mod.GetName()));
		}
		else
		{
			dialog.SetCaption(KTrf("ModManager.RemoveMod.CaptionErase", mod.GetName()));
			eraseLog = true;
		}

		if (dialog.ShowModal() == KxID_YES)
		{
			if (eraseLog)
			{
				IModManager::GetInstance()->EraseMod(mod);
			}
			else
			{
				IModManager::GetInstance()->UninstallMod(mod);
			}
		}
	}
	void Workspace::OnModSerach(wxCommandEvent& event)
	{
		if (m_DisplayModel->SetSearchMask(event.GetEventType() == wxEVT_SEARCHCTRL_SEARCH_BTN ? event.GetString() : wxEmptyString))
		{
			m_DisplayModel->LoadView();
		}
	}
	void Workspace::OnModSearchColumnsChanged(KxMenuEvent& event)
	{
		KxMenuItem* item = event.GetItem();
		item->Check(!item->IsChecked());

		KxDataView2::Column::RefVector columns;
		for (const auto& item: event.GetMenu()->GetMenuItems())
		{
			if (item->IsChecked())
			{
				columns.push_back(static_cast<KxDataView2::Column*>(static_cast<KxMenuItem*>(item)->GetClientData()));
			}
		}
		m_DisplayModel->SetSearchColumns(columns);
	}

	void Workspace::ClearControls()
	{
		m_SearchBox->Clear();
		IMainWindow::GetInstance()->ClearStatus();
	}
	void Workspace::DisplayModInfo(IGameMod* entry)
	{
		wxWindowUpdateLocker lock(this);
		ClearControls();

		if (entry)
		{
			IMainWindow::GetInstance()->SetStatus(entry->GetName());
		}
	}
	void Workspace::CreateViewContextMenu(KxMenu& contextMenu, const IGameMod::RefVector& selectedMods, IGameMod* focusedMod)
	{
		if (focusedMod)
		{
			Utility::MenuSeparatorAfter separator1(contextMenu);

			const bool isMultipleSelection = selectedMods.size() > 1;
			
			const bool isFixedMod = focusedMod->QueryInterface<FixedGameMod>();
			const bool isPriorityGroup = focusedMod->QueryInterface<PriorityGroup>();
			const bool isNormalMod = !isFixedMod && !isPriorityGroup;

			const bool isLinkedMod = focusedMod->IsLinkedMod();
			const bool isInstalled = focusedMod->IsInstalled();
			const bool isPackageExist = !isFixedMod && !isPriorityGroup && focusedMod->IsPackageFileExist();
			const bool isVFSActive = IModManager::GetInstance()->GetFileSystem().IsEnabled();

			// Install and uninstall
			if (isInstalled)
			{
				KxMenuItem* item = contextMenu.AddItem(ContextMenuID::ModUninstall, KTr("ModManager.Menu.UninstallMod"));
				item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::BoxMinus));
				item->Enable(!isMultipleSelection && !isVFSActive && isNormalMod);
			}
			else
			{
				KxMenuItem* item = contextMenu.AddItem(ContextMenuID::ModInstall, KTr("ModManager.Menu.InstallMod"));
				item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::Box));
				item->Enable(!isMultipleSelection && isPackageExist && isNormalMod);
			}
			{
				// Linked mods can't be uninstalled on erase
				KxMenuItem* item = contextMenu.AddItem(ContextMenuID::ModErase);
				item->SetItemLabel(isInstalled && !isLinkedMod ? KTr("ModManager.Menu.UninstallAndErase") : KTr("ModManager.Menu.Erase"));
				item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::Eraser));
				item->Enable(!isMultipleSelection && !isVFSActive && isNormalMod);
			}
			contextMenu.AddSeparator();
			{
				KxMenuItem* item = contextMenu.AddItem(ContextMenuID::ModEditDescription, KTr("ModManager.Menu.EditDescription"));
				item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::DocumentPencil));
				item->Enable(!isMultipleSelection && isNormalMod);
			}
			{
				KxMenuItem* item = contextMenu.AddItem(ContextMenuID::ModEditTags, KTr("ModManager.Menu.EditTags"));
				item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::Tags));
				item->Enable(!isFixedMod);
			}
			{
				KxMenuItem* item = contextMenu.AddItem(ContextMenuID::ModEditSources, KTr("ModManager.Menu.EditSites"));
				item->SetBitmap(ImageProvider::GetBitmap(IModNetwork::GetGenericIcon()));
				item->Enable(!isMultipleSelection && isNormalMod);
			}
			{
				KxMenuItem* item = contextMenu.AddItem(ContextMenuID::ModChangeID, KTr("ModManager.Menu.ChangeID"));
				item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::Key));
				item->Enable(!isMultipleSelection && !isVFSActive && isNormalMod);
			}

			// Package menu
			if (IPackageManager* packageManager = IPackageManager::GetInstance())
			{
				Utility::MenuSeparatorAfter separator(contextMenu);
				packageManager->OnModListMenu(contextMenu, selectedMods, focusedMod);
			}

			// Color menu
			if (KxMenu* colorMenu = new KxMenu(); true)
			{
				contextMenu.Add(colorMenu, KTr("Generic.Color"));
				{
					KxMenuItem* item = colorMenu->AddItem(ContextMenuID::ColorAssign, KTr("Generic.Assign"));
					item->SetDefault();
				}
				{
					KxMenuItem* item = colorMenu->AddItem(ContextMenuID::ColorReset, KTr("Generic.Reset"));
					item->Enable(focusedMod->HasColor() || isMultipleSelection);
				}
			}
			contextMenu.AddSeparator();

			// Other items
			{
				KxMenuItem* item = contextMenu.AddItem(ContextMenuID::ModOpenLocation, KTr("ModManager.Menu.OpenModFilesLocation"));
				item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::FolderOpen));
				item->Enable(!isMultipleSelection && !isPriorityGroup);
			}
			{
				KxMenuItem* item = contextMenu.AddItem(ContextMenuID::ModChangeLocation, KTr("ModManager.Menu.ChangeModFilesLocation"));
				item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::FolderArrow));
				item->Enable(!isMultipleSelection && isNormalMod && !isVFSActive);
			}
			if (!isMultipleSelection && isLinkedMod && isNormalMod)
			{
				KxMenuItem* item = contextMenu.AddItem(ContextMenuID::ModRevertLocation, KTr("ModManager.Menu.RevertModFilesLocation"));
				item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::FolderArrow));
			}
			{
				KxMenuItem* item = contextMenu.AddItem(ContextMenuID::ModProperties, KTr("ModManager.Menu.Properties"));
				item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::InformationFrame));
				item->Enable(!isMultipleSelection && isNormalMod);
			}
		}

		// Refresh
		{
			KxMenuItem* item = contextMenu.AddItem(KTr(KxID_REFRESH));
			item->SetBitmap(ImageProvider::GetBitmap(ImageResourceID::ArrowCircleDouble));
			item->Bind(KxEVT_MENU_SELECT, [this](KxMenuEvent& event)
			{
				IModManager::GetInstance()->Load();
				IModManager::GetInstance()->ResortMods();
				ScheduleReload();
			});
		}
	}

	void Workspace::SelectMod(const IGameMod* entry)
	{
		m_DisplayModel->SelectMod(entry);
	}
	void Workspace::ProcessSelection(IGameMod* entry)
	{
		DisplayModInfo(entry);
	}
	void Workspace::HighlightMod(const IGameMod* mod)
	{
		m_DisplayModel->GetView()->UnselectAll();
		m_DisplayModel->SelectMod(mod);
	}
	void Workspace::ReloadView()
	{
		m_DisplayModel->LoadView();
		ProcessSelection();
	}

	void Workspace::OnModsContextMenu(const IGameMod::RefVector& selectedMods, IGameMod* focusedMod)
	{
		// Get mouse position before doing anything else,
		// as mouse pointer can move before displaying showing the menu.
		wxPoint mousePos = wxGetMousePosition();

		// Create menu
		KxMenu contextMenu;
		CreateViewContextMenu(contextMenu, selectedMods, focusedMod);

		// Mod network menu for these mods
		for (auto& modNetwork: INetworkManager::GetInstance()->GetModNetworks())
		{
			modNetwork->OnModListMenu(contextMenu, selectedMods, focusedMod);
		}

		// Show menu
		const ContextMenuID menuID = (ContextMenuID)contextMenu.Show(nullptr, mousePos);
		switch (menuID)
		{
			// Mod
			case ContextMenuID::ModOpenLocation:
			{
				// Try to open mod files folder without displaying the error. If it fails open root folder with error message.
				if (!KxShell::Execute(this, focusedMod->GetModFilesDir(), {}, {}, {}, SW_SHOWNORMAL, true))
				{
					KxShell::Execute(this, focusedMod->GetRootDir());
				}
				break;
			}
			case ContextMenuID::ModChangeLocation:
			{
				KxFileBrowseDialog dialog(this, KxID_NONE, KxFBD_OPEN_FOLDER);
				if (dialog.ShowModal() == KxID_OK)
				{
					if (!focusedMod->IsInstalled() || KxShell::FileOperationEx(KxFOF_MOVE, focusedMod->GetModFilesDir() + "\\*", dialog.GetResult(), this, true, false, false, true))
					{
						KxFile(focusedMod->GetModFilesDir()).RemoveFolder(true);
						focusedMod->LinkLocation(dialog.GetResult());
						focusedMod->Save();

						BroadcastProcessor::Get().ProcessEvent(ModEvent::EvtFilesChanged, *focusedMod);
						BroadcastProcessor::Get().ProcessEvent(ModEvent::EvtChanged, *focusedMod);
					}
				}
				break;
			}
			case ContextMenuID::ModRevertLocation:
			{
				if (!focusedMod->IsInstalled() || KxShell::FileOperationEx(KxFOF_MOVE, focusedMod->GetModFilesDir() + "\\*", focusedMod->GetDefaultModFilesDir(), this, true, false, false, true))
				{
					// Remove file folder if it's empty
					KxFile(focusedMod->GetModFilesDir()).RemoveFolder(true);

					focusedMod->LinkLocation(wxEmptyString);
					focusedMod->Save();

					BroadcastProcessor::Get().ProcessEvent(ModEvent::EvtFilesChanged, *focusedMod);
					BroadcastProcessor::Get().ProcessEvent(ModEvent::EvtChanged, *focusedMod);
				}
				break;
			}
			case ContextMenuID::ModInstall:
			{
				InstallMod(*focusedMod);
				break;
			}
			case ContextMenuID::ModUninstall:
			case ContextMenuID::ModErase:
			{
				UninstallMod(*focusedMod, menuID == ContextMenuID::ModErase);
				break;
			}
			case ContextMenuID::ModEditDescription:
			{
				wxString oldDescription = focusedMod->GetDescription();
				UI::TextEditDialog dialog(this);
				dialog.SetText(oldDescription);

				if (dialog.ShowModal() == KxID_OK && dialog.IsModified())
				{
					focusedMod->SetDescription(dialog.GetText());
					focusedMod->Save();

					BroadcastProcessor::Get().ProcessEvent(ModEvent::EvtChanged, *focusedMod);
				}
				break;
			}
			case ContextMenuID::ModEditTags:
			{
				BasicGameMod tempMod;
				tempMod.GetTagStore() = focusedMod->GetTagStore();

				ModTagManager::SelectorDialog dialog(this, KTr("ModManager.TagsDialog"));
				dialog.SetDataVector(tempMod.GetTagStore(), tempMod);
				dialog.ShowModal();
				if (dialog.IsModified())
				{
					dialog.ApplyChanges();
					bool changesMade = DoForAllSelectedItems(selectedMods, [&tempMod](IGameMod& mod)
					{
						mod.GetTagStore() = tempMod.GetTagStore();
						mod.Save();
						return true;
					});
					if (changesMade)
					{
						BroadcastProcessor::Get().ProcessEvent(ModEvent::EvtChanged, selectedMods);
						ReloadView();
					}
				}
				break;
			}
			case ContextMenuID::ModEditSources:
			{
				ModSource::StoreDialog dialog(this, focusedMod->GetModSourceStore());
				dialog.ShowModal();
				if (dialog.IsModified())
				{
					focusedMod->Save();
					BroadcastProcessor::Get().ProcessEvent(ModEvent::EvtChanged, *focusedMod);
				}
				break;
			}
			case ContextMenuID::ModChangeID:
			{
				if (ShowChangeModIDDialog(*focusedMod))
				{
					m_DisplayModel->UpdateUI();
				}
				break;
			}
			case ContextMenuID::ModProperties:
			{
				KxShell::Execute(this, focusedMod->GetModFilesDir(), "properties");
				break;
			}

			// Color menu
			case ContextMenuID::ColorAssign:
			{
				wxColourData colorData;
				colorData.SetColour(focusedMod->GetColor());
				colorData.SetChooseFull(true);
				colorData.SetChooseAlpha(true);

				wxColourDialog dialog(this, &colorData);
				if (dialog.ShowModal() == wxID_OK)
				{
					DoForAllSelectedItems(selectedMods, [&dialog](IGameMod& mod)
					{
						mod.SetColor(dialog.GetColourData().GetColour());
						mod.Save();

						BroadcastProcessor::Get().ProcessEvent(ModEvent::EvtChanged, mod);
						return true;
					});
					m_DisplayModel->UpdateUI();
				}
				break;
			}
			case ContextMenuID::ColorReset:
			{
				DoForAllSelectedItems(selectedMods, [](IGameMod& mod)
				{
					mod.SetColor(KxColor());
					mod.Save();

					BroadcastProcessor::Get().ProcessEvent(ModEvent::EvtChanged, mod);
					return true;
				});
				m_DisplayModel->UpdateUI();
				break;
			}
		};
	}
	void Workspace::UpdateModListContent()
	{
		m_ToolBar_Profiles->Clear();
		int selectIndex = 0;
		for (const auto& profile: IGameInstance::GetActive()->GetProfiles())
		{
			int index = m_ToolBar_Profiles->Append(profile->GetID());
			if (profile->GetID() == IGameInstance::GetActiveProfileID())
			{
				selectIndex = index;
			}
		}
		m_ToolBar_Profiles->SetSelection(selectIndex);
	}

	bool Workspace::IsAnyChangeAllowed() const
	{
		return IsMovingModsAllowed();
	}
	bool Workspace::IsMovingModsAllowed() const
	{
		return m_DisplayModel->GetDisplayMode() == DisplayModelType::Connector && IsChangingModsAllowed();
	}
	bool Workspace::IsChangingModsAllowed() const
	{
		return !IModManager::GetInstance()->GetFileSystem().IsEnabled();
	}
}
