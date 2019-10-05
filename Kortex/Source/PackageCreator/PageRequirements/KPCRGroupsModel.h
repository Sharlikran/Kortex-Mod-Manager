#pragma once
#include "stdafx.h"
#include "PackageCreator/KPackageCreatorVectorModel.h"
#include "PackageCreator/KPackageCreatorIDTracker.h"
#include "PackageProject/KPackageProjectRequirements.h"
class KxButton;
class KxDataViewComboBox;

namespace Kortex::PackageDesigner
{
	class KPCREntriesListModel;
	class KPCRGroupsModel: public KPackageCreatorVectorModel<KPPRRequirementsGroup::Vector>, public KPackageCreatorIDTracker
	{
		private:
			KPackageProjectRequirements* m_Requirements = nullptr;
			KxDataViewComboBox* m_ComboView = nullptr;
			KPCREntriesListModel* m_EntriesModel = nullptr;
			KxButton* m_AddButton = nullptr;
			KxButton* m_RemoveButton = nullptr;
	
		private:
			KxDataViewCtrl* OnCreateDataView(wxWindow* window) override;
			wxWindow* OnGetDataViewWindow() override;
			void OnInitControl() override;
	
			void GetEditorValueByRow(wxAny& value, size_t row, const KxDataViewColumn* column) const override;
			void GetValueByRow(wxAny& value, size_t row, const KxDataViewColumn* column) const override;
			bool SetValueByRow(const wxAny& value, size_t row, const KxDataViewColumn* column) override;
	
			void OnSelectItem(KxDataViewEvent& event);
			void OnActivateItem(KxDataViewEvent& event);
			void OnContextMenu(KxDataViewEvent& event);
			void OnGetStringValue(KxDataViewEvent& event);
	
			void OnAddGroup(bool useDialog = false);
			void OnRemoveGroup(const KxDataViewItem& item);
			void OnClearList();
			bool OnInsertItem(KxDataViewItem& currentItem, KxDataViewItem& droppedItem) override
			{
				OnInsertItemHelperUniquePtr(*GetDataVector(), currentItem, droppedItem);
				return true;
			}
	
			void RefreshComboControl();
			bool DoTrackID(const wxString& trackedID, const wxString& newID, bool remove) const;
	
		protected:
			KPackageCreatorIDTracker* GetTracker() override
			{
				return this;
			}
			bool TrackChangeID(const wxString& trackedID, const wxString& newID) override
			{
				return DoTrackID(trackedID, newID, false);
			}
			bool TrackRemoveID(const wxString& trackedID) override
			{
				return DoTrackID(trackedID, wxEmptyString, true);
			}
	
		public:
			void Create(KPackageCreatorController* controller, wxWindow* window, wxSizer* sizer) override;
			void ChangeNotify() override;
	
			KPPRRequirementsGroup* GetDataEntry(size_t index) const
			{
				if (index < GetItemCount())
				{
					return (*GetDataVector())[index].get();
				}
				return nullptr;
			}
			KPPRRequirementsGroup* GetDataEntry(const KxDataViewItem& item) const
			{
				return GetDataEntry(GetRow(item));
			}
			
			void SetProject(KPackageProject& projectData);
			void SetEntriesModel(KPCREntriesListModel* model)
			{
				m_EntriesModel = model;
			}
	};
}
