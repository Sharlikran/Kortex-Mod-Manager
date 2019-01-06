#include "stdafx.h"
#include "KInstanceSelectionDialog.h"
#include <Kortex/Application.hpp>
#include <Kortex/GameInstance.hpp>
#include <Kortex/Events.hpp>
#include "UI/KInstanceCreatorDialog.h"
#include "Utility/KBitmapSize.h"
#include "Utility/KAux.h"
#include <KxFramework/KxTaskDialog.h>
#include <KxFramework/KxSplitterWindow.h>
#include <KxFramework/KxBitmapComboBox.h>
#include <KxFramework/KxListBox.h>
#include <KxFramework/KxTextBox.h>
#include <KxFramework/KxButton.h>
#include <KxFramework/KxFile.h>
#include <KxFramework/KxFileBrowseDialog.h>
#include <KxFramework/KxShell.h>
#include <KxFramework/KxShellLink.h>
#include <KxFramework/KxLibrary.h>
#include <KxFramework/KxSystem.h>

namespace Kortex
{
	bool KInstanceSelectionDialog::Create(wxWindow* parent,
										  wxWindowID id,
										  const wxString& caption,
										  const wxPoint& pos,
										  const wxSize& size,
										  int buttons,
										  long style
	)
	{
		if (KxComboBoxDialog::Create(parent, id, caption, pos, size, buttons, style))
		{
			SetMainIcon(KxICON_NONE);
			SetWindowResizeSide(wxBOTH);

			// Bottom panel
			m_OK = GetButton(KxID_OK).As<KxButton>();
			m_CreateShortcut = AddButton(KxID_NONE, KTr("InstanceSelection.CreateShortcut"), true).As<KxButton>();
			m_Remove = AddButton(KxID_REMOVE, KTr("InstanceSelection.RemoveInstance"), true).As<KxButton>();
			m_Create = AddButton(KxID_ADD, KTr("InstanceSelection.CreateInstance"), true).As<KxButton>();

			// Splitter
			wxSizer* mainSizer = GetContentWindowSizer();
			m_Splitter = new KxSplitterWindow(GetContentWindow(), KxID_NONE);
			IThemeManager::GetActive().ProcessWindow(m_Splitter);
			m_Splitter->SetMinimumPaneSize(200);

			// Left pane
			m_LeftPane = new KxPanel(m_Splitter, KxID_NONE);
			m_LeftSizer = new wxBoxSizer(wxVERTICAL);
			m_LeftPane->SetSizer(m_LeftSizer);

			mainSizer->Detach(GetDialogMainCtrl());
			GetDialogMainCtrl()->Reparent(m_LeftPane);
			m_LeftSizer->Add(GetDialogMainCtrl(), 0, wxEXPAND);
			m_GameFilter = static_cast<KxBitmapComboBox*>(GetDialogMainCtrl());

			m_InstancesList = new KxListBox(m_LeftPane, KxID_NONE);
			m_LeftSizer->Add(m_InstancesList, 1, wxEXPAND|wxTOP, KLC_VERTICAL_SPACING);
			AddUserWindow(m_InstancesList);

			// Right pane
			m_RightPane = new KxPanel(m_Splitter, KxID_NONE);
			m_RightSizer = new wxBoxSizer(wxVERTICAL);
			m_RightPane->SetSizer(m_RightSizer);

			m_TextBox = new KxTextBox(m_RightPane, KxID_NONE, wxEmptyString, KxTextBox::DefaultStyle|wxTE_MULTILINE);
			m_TextBox->SetEditable(false);
			m_RightSizer->Add(m_TextBox, 1, wxEXPAND);
			AddUserWindow(m_TextBox);

			// Done
			m_Splitter->SplitVertically(m_LeftPane, m_RightPane, 300);
			mainSizer->Add(m_Splitter, 1, wxEXPAND);

			m_CreateShortcut->Bind(wxEVT_BUTTON, &KInstanceSelectionDialog::OnCreateShortcut, this);
			return true;
		}
		return false;
	}

	KInstanceSelectionDialog::KInstanceSelectionDialog(wxWindow* parent)
	{
		Create(parent, KxID_NONE, KTr("InstanceSelection.Caption"), wxDefaultPosition, wxDefaultSize, KxBTN_OK|KxBTN_CANCEL, DefaultStyle|KxCBD_READONLY|KxCBD_BITMAP);
		Configure();
	}
	KInstanceSelectionDialog::~KInstanceSelectionDialog()
	{
	}

	GameID KInstanceSelectionDialog::GetCurrentFilter() const
	{
		void* data = m_GameFilter->GetClientData(m_GameFilter->GetSelection());
		return data ? static_cast<IGameInstance*>(data)->GetGameID() : GameIDs::NullGameID;
	}
	IGameInstance* KInstanceSelectionDialog::GetCurrentInstance() const
	{
		void* data = (void*)m_InstancesList->GetItemData(m_InstancesList->GetSelection());
		if (data)
		{
			return static_cast<IGameInstance*>(data);
		}
		return nullptr;
	}

	void KInstanceSelectionDialog::Configure()
	{
		Bind(KxEVT_STDDIALOG_BUTTON, &KInstanceSelectionDialog::OnButton, this);
		Bind(Kortex::Events::ProfileRefreshList, &KInstanceSelectionDialog::OnUpdateProfiles, this);

		m_GameFilter->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& event)
		{
			OnFilterSelected(GetCurrentFilter());
		});
		m_InstancesList->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxCommandEvent& event)
		{
			OnInstanceSelected(GetCurrentInstance());
			event.Skip();
		});
		m_InstancesList->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxCommandEvent& event)
		{
			OnInstanceSelected(GetCurrentInstance());
			event.Skip();
		});

		const IGameInstance* active = IGameInstance::GetActive();
		LoadGameFilter(active ? active->GetGameID() : GameIDs::NullGameID);

		wxString instanceID = active ? active->GetInstanceID() : IApplication::GetInstance()->GetStartupInstanceID();
		OnInstanceSelected(IGameInstance::GetShallowInstance(instanceID));

		AdjustWindow(wxDefaultPosition, wxSize(650, 400));
	}
	void KInstanceSelectionDialog::LoadGameFilter(const GameID& gameID)
	{
		KBitmapSize bitmapSize = KBitmapSize().FromSystemIcon();
		m_GameFilterImageList = new KxImageList(bitmapSize.GetWidth(), bitmapSize.GetHeight(), false, IGameInstance::GetTemplatesCount());
		m_GameFilter->AssignImageList(m_GameFilterImageList);

		int imageID = m_GameFilterImageList->Add(IGameInstance::GetGenericIcon());
		m_GameFilter->AddItem(KAux::MakeBracketedLabel(KTr("Generic.All")), imageID);

		int select = 0;
		for (const auto& currentTemplate: IGameInstance::GetTemplates())
		{
			imageID = m_GameFilterImageList->Add(currentTemplate->GetIcon());
			int index = m_GameFilter->AddItem(currentTemplate->GetGameName(), imageID);
			m_GameFilter->SetClientData(index, static_cast<void*>(currentTemplate.get()));

			if (gameID.IsOK() && gameID == currentTemplate->GetGameID())
			{
				select = index;
			}
		}
		m_GameFilter->SetSelection(select);

		wxCommandEvent event(wxEVT_COMBOBOX);
		event.SetInt(select);
		m_GameFilter->ProcessWindowEvent(event);
	}
	void KInstanceSelectionDialog::LoadInstancesList(const GameID& gameID, IGameInstance* selectInstance)
	{
		m_OK->Disable();
		m_Remove->Disable();
		m_CreateShortcut->Disable();
		m_InstancesList->ClearItems();

		int select = 0;
		for (const auto& instnace: IGameInstance::GetShallowInstances())
		{
			if (gameID && gameID != instnace->GetGameID())
			{
				continue;
			}

			wxString name;
			if (instnace->IsOK())
			{
				if (gameID)
				{
					name = instnace->GetInstanceID();
				}
				else
				{
					name = KxString::Format("[%1] %2", instnace->GetGameShortName(), instnace->GetInstanceID());
				}
			}
			else
			{
				name = KxString::Format("%1 (%2)", instnace->GetInstanceID(), KTr("InstanceSelection.InvalidInstance"));
			}

			int index = m_InstancesList->AddItem(name);
			m_InstancesList->SetItemPtrData(index, reinterpret_cast<wxUIntPtr>(instnace.get()));
			if (selectInstance && selectInstance == instnace.get())
			{
				select = index;
			}
		}
		m_InstancesList->Select(select);
		m_InstancesList->SetFocus();

		wxCommandEvent event(wxEVT_LIST_ITEM_SELECTED);
		event.SetInt(select);
		m_GameFilter->HandleWindowEvent(event);
	}
	bool KInstanceSelectionDialog::AskForGameFolder(const IGameInstance* instance, const wxString& currentGamePath)
	{
		KxTaskDialog messageDialog(this, KxID_NONE, KTrf("InstanceSelection.GameNotFound.Caption", instance->GetGameName()), KTrf("InstanceSelection.GameNotFound.Message", instance->GetGameName()), KxBTN_CANCEL, KxICON_WARNING);
		messageDialog.AddButton(KxID_SELECT_FOLDER);
		if (messageDialog.ShowModal() == KxID_SELECT_FOLDER)
		{
			KxFileBrowseDialog browseDialog(this, KxID_NONE, KxFBD_OPEN_FOLDER);
			browseDialog.SetFolder(currentGamePath);
			if (browseDialog.ShowModal())
			{
				m_SelectedGameRoot = browseDialog.GetResult();
				return true;
			}
		}

		m_SelectedGameRoot.Clear();
		return false;
	}

	void KInstanceSelectionDialog::OnFilterSelected(const GameID& gameID)
	{
		LoadInstancesList(gameID, GetCurrentInstance());
	}
	void KInstanceSelectionDialog::OnInstanceSelected(IGameInstance* instance)
	{
		if (instance)
		{
			m_OK->Enable(instance->IsOK());
			m_Remove->Enable(!instance->IsActiveInstance() && instance->IsDeployed());
			m_CreateShortcut->Enable(instance->IsOK());

			OnDisplayInstanceInfo(instance);
		}
		else
		{
			m_OK->Enable(false);
			m_Remove->Enable(false);
			m_CreateShortcut->Enable(false);

			OnDisplayInstanceInfo(nullptr);
		}
	}

	void KInstanceSelectionDialog::OnCreateShortcut(wxCommandEvent& event)
	{
		IGameInstance* instance = GetCurrentInstance();
		if (instance)
		{
			KxFileBrowseDialog dialog(this, KxID_NONE, KxFBD_SAVE);
			dialog.SetDefaultExtension("lnk");
			dialog.SetFileName(wxString::Format("%s - %s", instance->GetGameShortName(), instance->GetInstanceID()));
			dialog.SetOptionEnabled(KxFBD_NO_DEREFERENCE_LINKS);

			dialog.AddFilter("*.lnk", KTr("FileFilter.Shortcuts"));
			dialog.AddFilter("*", KTr("FileFilter.AllFiles"));

			if (dialog.ShowModal() == KxID_OK)
			{
				KxShellLink link;
				link.SetTarget(KxLibrary(nullptr).GetFileName());
				link.SetArguments(KxString::Format("InstanceID \"%2\"", instance->GetInstanceID()));
				link.SetWorkingFolder(KxFile::GetCWD());
				link.SetIconLocation(instance->GetIconLocation());
				link.Save(dialog.GetResult());
			}
		}
	}
	void KInstanceSelectionDialog::OnButton(wxNotifyEvent& event)
	{
		wxWindowID id = event.GetId();
		GameID selectedGame = GetCurrentFilter();
		IGameInstance* instance = GetCurrentInstance();

		if (id == KxID_ADD || id == KxID_REMOVE)
		{
			event.Veto();

			if (selectedGame && id == KxID_ADD)
			{
				KInstanceCreatorDialog dialog(this, IGameInstance::GetTemplate(selectedGame));
				if (dialog.ShowModal() == KxID_OK)
				{
					LoadInstancesList(selectedGame, IGameInstance::GetShallowInstance(dialog.GetName()));
				}
			}
			if (instance && id == KxID_REMOVE)
			{
				wxWindowID ret = KxTaskDialog(this,
											  KxID_NONE,
											  KTrf("InstanceSelection.ConfirmRemoving.Caption", instance->GetInstanceID()),
											  KTrf("InstanceSelection.ConfirmRemoving.Message", instance->GetInstanceDir(), instance->GetInstanceID()),
											  KxBTN_YES|KxBTN_NO, KxICON_WARNING
				).ShowModal();
				if (ret == KxID_YES)
				{
					if (instance->WithdrawDeploy())
					{
						instance = nullptr;
						LoadInstancesList(selectedGame);
					}
					else
					{
						LogEvent(KTr("InstanceCreatorDialog.DeletionError"), LogLevel::Error).Send();
					}
				}
			}
		}
		else if (id == KxID_OK)
		{
			m_SelectedInstance = nullptr;
			wxString gamePath = instance->GetGameDir();

			// Try defaults
			if (gamePath.IsEmpty())
			{
				gamePath = instance->GetTemplate().GetGameID();
			}

			// Ask user
			if (!KxFile(gamePath).IsFolderExist())
			{
				if (AskForGameFolder(instance, gamePath))
				{
					instance->GetVariables().SetVariable(Variables::KVAR_ACTUAL_GAME_DIR, m_SelectedGameRoot);

					IConfigurableGameInstance* configurableInstance = nullptr;
					if (instance->QueryInterface(configurableInstance))
					{
						configurableInstance->SaveConfig();
					}
				}
				else
				{
					return;
				}
			}

			m_SelectedInstance = instance;
			event.Skip();
		}
	}
	void KInstanceSelectionDialog::OnUpdateProfiles(wxNotifyEvent& event)
	{
		LoadInstancesList(GetCurrentFilter(), IGameInstance::GetShallowInstance(event.GetString()));
	}
	void KInstanceSelectionDialog::OnDisplayInstanceInfo(const IGameInstance* instance)
	{
		auto PrintSeparator = [this]()
		{
			*m_TextBox << wxS("\r\n\r\n");
		};
		auto PrintVariables = [this](const IVariableTable& variables)
		{
			variables.Accept([this](const wxString& name, const VariableValue& value)
			{
				*m_TextBox << KxFormat(wxS("$(%1)%2 = \"%3\"")).arg(name).arg(value.IsOverride() ? wxS("*") : wxS("")).arg(value) << wxS("\r\n");
				return true;
			});
		};

		wxWindowUpdateLocker lock(m_TextBox);
		m_TextBox->Clear();
		m_TextBox->Disable();

		if (instance)
		{
			if (instance->IsActiveInstance())
			{
				instance = IGameInstance::GetActive();
			}
			m_TextBox->Enable();

			// Instance variables
			PrintVariables(instance->GetVariables());
			PrintSeparator();

			// App variables
			PrintVariables(IApplication::GetInstance()->GetVariables());
			PrintSeparator();

			// $SHF(*) variables
			for (const auto&[id, item]: KxShell::GetShellFolderList())
			{
				// Skip 'SHF_' part for 'item.second'
				*m_TextBox << KxFormat(wxS("$SHF(%1) = \"%2\"")).arg(item.second + 4).arg(KxShell::GetFolder(id)) << wxS("\r\n");
			}
			PrintSeparator();

			// $ENV(*) variables
			for (const auto&[name, value]: KxSystem::GetEnvironmentVariables())
			{
				*m_TextBox << KxFormat(wxS("$ENV(%1) = \"%2\"")).arg(name).arg(value) << wxS("\r\n");
			}
		}
	}
}
