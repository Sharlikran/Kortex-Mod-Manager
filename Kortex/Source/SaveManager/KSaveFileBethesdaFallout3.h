#pragma once
#include "stdafx.h"
#include "KSaveFile.h"

class KSaveFileBethesdaFallout3: public KSaveFile
{
	private:
		KLabeledValueArray m_BasicInfo;
		KxStringVector m_PluginsList;
		wxBitmap m_Bitmap = wxNullBitmap;

	protected:
		virtual bool DoReadData() override;

	public:
		KSaveFileBethesdaFallout3(const wxString& filePath)
			:KSaveFile(filePath)
		{
		}

	public:
		virtual const KLabeledValueArray& GetBasicInfo() const override
		{
			return m_BasicInfo;
		}
		virtual const KxStringVector& GetPluginsList() const override
		{
			return m_PluginsList;
		}
		virtual const wxBitmap& GetBitmap() const override
		{
			return m_Bitmap;
		}
};
