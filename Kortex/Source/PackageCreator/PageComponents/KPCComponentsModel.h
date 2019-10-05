#pragma once
#include "stdafx.h"
#include "PackageCreator/KPackageCreatorVectorModel.h"
#include "PackageCreator/PageComponents/KPCComponentsModelNode.h"
#include "PackageProject/KPackageProject.h"
#include "PackageProject/KPackageProjectComponents.h"
#include <KxFramework/KxDataView.h>
class KxImageView;

namespace Kortex::PackageDesigner
{
	class KPackageCreatorController;
	class KPackageProjectInterface;
	class KPackageProjectComponents;
}

namespace Kortex::PackageDesigner
{
	class KPCComponentsModel:
		public KxDataViewModelExBase<KxDataViewModel>,
		public KxDataViewModelExDragDropEnabled<KPackageCreatorListModelDataObject>
	{
		public:
			using EntryID = KPCComponentsModelNode::EntryID;
			using AllItemsFunc = void(KPCComponentsModel::*)(KPCComponentsModelNode*, const wxString&);
	
		private:
			KPCComponentsModelNode::Vector m_Steps;
			KPackageCreatorController* m_Controller = nullptr;
	
			KxDataViewComboBoxEditor m_TypeDescriptorEditor;
			KxDataViewComboBoxEditor m_SelectionModeEditor;
			KxDataViewComboBoxEditor m_ImagesEditor;
			KxDataViewTextEditor m_TextEditor;
			KxImageView* m_EntryImageView = nullptr;
	
		protected:
			void OnInitControl() override;
			
			bool IsContainer(const KxDataViewItem& item) const override;
			void GetChildren(const KxDataViewItem& item, KxDataViewItem::Vector& children) const override;
			KxDataViewItem GetParent(const KxDataViewItem& item) const override;
			bool HasContainerColumns(const KxDataViewItem& item) const override;
	
			bool IsEnabled(const KxDataViewItem& item, const KxDataViewColumn* column) const override;
			bool IsEditorEnabled(const KxDataViewItem& item, const KxDataViewColumn* column) const override;
			void GetEditorValue(wxAny& value, const KxDataViewItem& item, const KxDataViewColumn* column) const override;
			void GetValue(wxAny& value, const KxDataViewItem& item, const KxDataViewColumn* column) const override;
			bool SetValue(const wxAny& value, const KxDataViewItem& item, const KxDataViewColumn* column) override;
			bool GetItemAttributes(const KxDataViewItem& item, const KxDataViewColumn* column, KxDataViewItemAttributes& attributes, KxDataViewCellState cellState) const override;
	
			void GetStepValue(wxAny& value, const KxDataViewColumn* column, const KPPCStep* step, bool editor = false) const;
			void GetGroupValue(wxAny& value, const KxDataViewColumn* column, const KPPCGroup* group, bool editor = false) const;
			void GetEntryValue(wxAny& value, const KxDataViewColumn* column, const KPPCEntry* entry, bool editor = false) const;
			void GetEntryItemValue(wxAny& value, const KxDataViewColumn* column, const KPPCEntry* entry, EntryID id, bool editor = false) const;
	
			bool SetStepValue(const wxAny& value, const KxDataViewColumn* column, KPPCStep* step);
			bool SetGroupValue(const wxAny& value, const KxDataViewColumn* column, KPPCGroup* group);
			bool SetEntryValue(const wxAny& value, const KxDataViewColumn* column, KPPCEntry* entry);
			bool SetEntryItemValue(const wxAny& value, const KxDataViewColumn* column, KPPCEntry* entry, EntryID id);
	
		private:
			void OnSelectItem(KxDataViewEvent& event);
			void OnActivateItem(KxDataViewEvent& event);
			void OnContextMenu(KxDataViewEvent& event);
	
			void OnAttachEditor(KxDataViewEvent& event);
			void OnDetachEditor(KxDataViewEvent& event);
			void UpdateImageEditorList();
	
			KxDataViewCtrl* GetViewCtrl() const override
			{
				return GetView();
			}
			bool OnDragItems(KxDataViewEventDND& event) override;
			bool OnDropItems(KxDataViewEventDND& event) override;
			bool OnDropItemsPossible(KxDataViewEventDND& event) override;
	
			void AddStep(KPCComponentsModelNode* node, const KxDataViewItem& item);
			void AddGroup(KPCComponentsModelNode* node, const KxDataViewItem& item);
			void AddEntry(KPCComponentsModelNode* node, KxDataViewItem& item);
			void AddEntriesFromFiles(KPCComponentsModelNode* node, KxDataViewItem& item);
			void RemoveStep(KPCComponentsModelNode* node, const KPPCStep* step);
			void RemoveGroup(KPCComponentsModelNode* node, const KPPCGroup* group);
			void RemoveEntry(KPCComponentsModelNode* node, const KPPCEntry* entry);
	
			KxMenu* CreateAllItemsMenu();
			void CreateAllItemsMenuEntry(KxMenu* menu, KPCComponentsModelNode* node, const wxString& name, AllItemsFunc func);
	
			void AllSteps_Name(KPCComponentsModelNode* node, const wxString& name);
			void AllSteps_Conditions(KPCComponentsModelNode* node, const wxString& name);
	
			void AllGroups_Name(KPCComponentsModelNode* node, const wxString& name);
			void AllGroups_SelectionMode(KPCComponentsModelNode* node, const wxString& name);
	
			void AllEntries_Name(KPCComponentsModelNode* node, const wxString& name);
			void AllEntries_DefaultTypeDescriptor(KPCComponentsModelNode* node, const wxString& name);
			void AllEntries_FileData(KPCComponentsModelNode* node, const wxString& name);
			void AllEntries_Requirements(KPCComponentsModelNode* node, const wxString& name);
			void AllEntries_Image(KPCComponentsModelNode* node, const wxString& name);
			void AllEntries_Conditions(KPCComponentsModelNode* node, const wxString& name);
			void AllEntries_AssignedFlags(KPCComponentsModelNode* node, const wxString& name);
			void AllEntries_Description(KPCComponentsModelNode* node, const wxString& name);
	
			KPackageProjectInterface& GetInterface() const;
			KPackageProjectComponents& GetComponents() const;
	
		public:
			KPCComponentsModel(KPackageCreatorController* controller);
	
		public:
			KxDataViewItem GetItem(const KPCComponentsModelNode* node) const
			{
				return KxDataViewItem(node);
			}
			KPCComponentsModelNode* GetNode(const KxDataViewItem& item) const
			{
				return item.GetValuePtr<KPCComponentsModelNode>();
			}
	
			void ChangeNotify();
			void NotifyChangedItem(const KxDataViewItem& item);
			void SetEntryImageView(KxImageView* pEntryImage)
			{
				m_EntryImageView = pEntryImage;
			}
			
			void RefreshItems() override;
			void SetProject(KPackageProject& project);
	};
}
