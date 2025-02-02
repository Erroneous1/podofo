/**
 * SPDX-FileCopyrightText: (C) 2005 Dominik Seichter <domseichter@web.de>
 * SPDX-FileCopyrightText: (C) 2020 Francesco Pretto <ceztko@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <podofo/private/PdfDeclarationsPrivate.h>
#include "PdfPage.h"

#include "PdfDictionary.h"
#include "PdfVariant.h"
#include "PdfWriter.h"
#include "PdfObjectStream.h"
#include "PdfColor.h"
#include "PdfDocument.h"

using namespace std;
using namespace PoDoFo;

static int normalize(int value, int start, int end);
static PdfResources* getResources(PdfObject& obj, const deque<PdfObject*>& listOfParents);

PdfPage::PdfPage(PdfDocument& parent, unsigned index, const Rect& size) :
    PdfDictionaryElement(parent, "Page"),
    m_Index(index),
    m_Contents(nullptr),
    m_Annotations(*this)
{
    initNewPage(size);
}

PdfPage::PdfPage(PdfObject& obj, unsigned index, const deque<PdfObject*>& listOfParents) :
    PdfDictionaryElement(obj),
    m_Index(index),
    m_Contents(nullptr),
    m_Resources(::getResources(obj, listOfParents)),
    m_Annotations(*this)
{
    PdfObject* contents = obj.GetDictionary().FindKey("Contents");
    if (contents != nullptr)
        m_Contents.reset(new PdfContents(*this, *contents));
}

Rect PdfPage::GetRect() const
{
    return this->GetMediaBox();
}

Rect PdfPage::GetRectRaw() const
{
    return this->GetMediaBox(true);
}

void PdfPage::SetRect(const Rect& rect)
{
    SetMediaBox(rect);
}

void PdfPage::SetRectRaw(const Rect& rect)
{
    SetMediaBox(rect, true);
}

bool PdfPage::HasRotation(double& teta) const
{
    int rotationRaw = normalize(GetRotationRaw(), 0, 360);
    if (rotationRaw == 0)
    {
        teta = 0;
        return false;
    }

    // Convert to radians and make it a counterclockwise rotation,
    // as common mathematical notation for rotations
    teta = -rotationRaw * DEG2RAD;
    return true;
}

void PdfPage::initNewPage(const Rect& size)
{
    SetMediaBox(size);
}

void PdfPage::ensureContentsCreated()
{
    if (m_Contents != nullptr)
        return;

    m_Contents.reset(new PdfContents(*this));
    GetDictionary().AddKey(PdfName::KeyContents,
        m_Contents->GetObject().GetIndirectReference());
}

void PdfPage::ensureResourcesCreated()
{
    if (m_Resources != nullptr)
        return;

    m_Resources.reset(new PdfResources(GetDictionary()));
}

PdfObjectStream& PdfPage::GetStreamForAppending(PdfStreamAppendFlags flags)
{
    ensureContentsCreated();
    return m_Contents->GetStreamForAppending(flags);
}

Rect PdfPage::CreateStandardPageSize(const PdfPageSize pageSize, bool landscape)
{
    Rect rect;

    switch (pageSize)
    {
        case PdfPageSize::A0:
            rect.Width = 2384;
            rect.Height = 3370;
            break;

        case PdfPageSize::A1:
            rect.Width = 1684;
            rect.Height = 2384;
            break;

        case PdfPageSize::A2:
            rect.Width = 1191;
            rect.Height = 1684;
            break;

        case PdfPageSize::A3:
            rect.Width = 842;
            rect.Height = 1190;
            break;

        case PdfPageSize::A4:
            rect.Width = 595;
            rect.Height = 842;
            break;

        case PdfPageSize::A5:
            rect.Width = 420;
            rect.Height = 595;
            break;

        case PdfPageSize::A6:
            rect.Width = 297;
            rect.Height = 420;
            break;

        case PdfPageSize::Letter:
            rect.Width = 612;
            rect.Height = 792;
            break;

        case PdfPageSize::Legal:
            rect.Width = 612;
            rect.Height = 1008;
            break;

        case PdfPageSize::Tabloid:
            rect.Width = 792;
            rect.Height = 1224;
            break;

        default:
            break;
    }

    if (landscape)
    {
        double tmp = rect.Width;
        rect.Width = rect.Height;
        rect.Height = tmp;
    }

    return rect;
}

Rect PdfPage::getPageBox(const string_view& inBox, bool raw) const
{
    Rect pageBox;

    // Take advantage of inherited values - walking up the tree if necessary
    auto obj = GetDictionary().FindKeyParent(inBox);

    // assign the value of the box from the array
    if (obj != nullptr && obj->IsArray())
    {
        pageBox = Rect::FromArray(obj->GetArray());
    }
    else if (inBox == "ArtBox" ||
        inBox == "BleedBox" ||
        inBox == "TrimBox")
    {
        // If those page boxes are not specified then
        // default to CropBox per PDF Spec (3.6.2)
        pageBox = getPageBox("CropBox", raw);
    }
    else if (inBox == "CropBox")
    {
        // If crop box is not specified then
        // default to MediaBox per PDF Spec (3.6.2)
        pageBox = getPageBox("MediaBox", raw);
    }

    if (!raw)
    {
        switch (GetRotationRaw())
        {
            case 90:
            case 270:
            case -90:
            case -270:
            {
                double temp = pageBox.Width;
                pageBox.Width = pageBox.Height;
                pageBox.Height = temp;
                break;
            }
            case 0:
            case 180:
            case -180:
                break;
            default:
                throw runtime_error("Invalid rotation");
        }
    }

    return pageBox;
}

void PdfPage::setPageBox(const string_view& inBox, const Rect& rect, bool raw)
{
    auto actualRect = rect;
    if (!raw)
    {
        switch (GetRotationRaw())
        {
            case 90:
            case 270:
            case -90:
            case -270:
            {
                actualRect.Width = rect.Height;
                actualRect.Height = rect.Width;
                break;
            }
            case 0:
            case 180:
            case -180:
                break;
            default:
                throw runtime_error("Invalid rotation");
        }
    }

    PdfArray mediaBox;
    actualRect.ToArray(mediaBox);
    this->GetDictionary().AddKey(inBox, mediaBox);
}

int PdfPage::GetRotationRaw() const
{
    int rot = 0;

    auto obj = GetDictionary().FindKeyParent("Rotate");
    if (obj != nullptr && (obj->IsNumber() || obj->GetReal()))
        rot = static_cast<int>(obj->GetNumber());

    return rot;
}

void PdfPage::SetRotationRaw(int rotation)
{
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)
        PODOFO_RAISE_ERROR(PdfErrorCode::ValueOutOfRange);

    this->GetDictionary().AddKey("Rotate", PdfVariant(static_cast<int64_t>(rotation)));
}

void PdfPage::MoveAt(unsigned index)
{
    // TODO: CHECK-ME FOR CORRECT WORKING
    auto& doc = GetDocument();
    auto& pages = doc.GetPages();
    unsigned fromIndex = m_Index;
    pages.InsertDocumentPageAt(index, doc, m_Index);
    if (index < fromIndex)
    {
        // If we inserted the page before the old 
        // position we have to increment the from position
        fromIndex++;
    }

    pages.RemovePageAt(fromIndex);
    m_Index = fromIndex;
}

PdfField& PdfPage::CreateField(const string_view& name, PdfFieldType fieldType, const Rect& rect, bool rawRect)
{
    auto& annotation = static_cast<PdfAnnotationWidget&>(GetAnnotations()
        .CreateAnnot(PdfAnnotationType::Widget, rect, rawRect));
    return PdfField::Create(name, annotation, fieldType);
}

PdfField& PdfPage::createField(const string_view& name, const type_info& typeInfo, const Rect& rect, bool rawRect)
{
    auto& annotation = static_cast<PdfAnnotationWidget&>(GetAnnotations()
        .CreateAnnot(PdfAnnotationType::Widget, rect, rawRect));
    return PdfField::Create(name, annotation, typeInfo);
}

void PdfPage::EnsureResourcesCreated()
{
    ensureResourcesCreated();
}

bool PdfPage::SetPageWidth(int newWidth)
{
    // Take advantage of inherited values - walking up the tree if necessary
    auto mediaBoxObj = GetDictionary().FindKeyParent("MediaBox");

    // assign the value of the box from the array
    if (mediaBoxObj != nullptr && mediaBoxObj->IsArray())
    {
        auto& mediaBoxArr = mediaBoxObj->GetArray();

        // in Rect::FromArray(), the Left value is subtracted from Width
        double dLeftMediaBox = mediaBoxArr[0].GetReal();
        mediaBoxArr[2] = PdfObject(newWidth + dLeftMediaBox);

        // Take advantage of inherited values - walking up the tree if necessary
        auto cropBoxObj = GetDictionary().FindKeyParent("CropBox");

        if (cropBoxObj != nullptr && cropBoxObj->IsArray())
        {
            auto& cropBoxArr = cropBoxObj->GetArray();
            // in Rect::FromArray(), the Left value is subtracted from Width
            double dLeftCropBox = cropBoxArr[0].GetReal();
            cropBoxArr[2] = PdfObject(newWidth + dLeftCropBox);
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

bool PdfPage::SetPageHeight(int newHeight)
{
    // Take advantage of inherited values - walking up the tree if necessary
    auto obj = GetDictionary().FindKeyParent("MediaBox");

    // assign the value of the box from the array
    if (obj != nullptr && obj->IsArray())
    {
        auto& mediaBoxArr = obj->GetArray();
        // in Rect::FromArray(), the Bottom value is subtracted from Height
        double bottom = mediaBoxArr[1].GetReal();
        mediaBoxArr[3] = PdfObject(newHeight + bottom);

        // Take advantage of inherited values - walking up the tree if necessary
        auto cropBoxObj = GetDictionary().FindKeyParent("CropBox");

        if (cropBoxObj != nullptr && cropBoxObj->IsArray())
        {
            auto& cropBoxArr = cropBoxObj->GetArray();
            // in Rect::FromArray(), the Bottom value is subtracted from Height
            double dBottomCropBox = cropBoxArr[1].GetReal();
            cropBoxArr[3] = PdfObject(newHeight + dBottomCropBox);
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

void PdfPage::SetMediaBox(const Rect& rect, bool raw)
{
    setPageBox("MediaBox", rect, raw);
}

void PdfPage::SetCropBox(const Rect& rect, bool raw)
{
    setPageBox("CropBox", rect, raw);
}

void PdfPage::SetTrimBox(const Rect& rect, bool raw)
{
    setPageBox("TrimBox", rect, raw);
}

void PdfPage::SetBleedBox(const Rect& rect, bool raw)
{
    setPageBox("BleedBox", rect, raw);
}

void PdfPage::SetArtBox(const Rect& rect, bool raw)
{
    setPageBox("ArtBox", rect, raw);
}

unsigned PdfPage::GetPageNumber() const
{
    unsigned pageNumber = 0;
    auto parent = this->GetDictionary().FindKey("Parent");
    PdfReference ref = this->GetObject().GetIndirectReference();

    // CVE-2017-5852 - prevent infinite loop if Parent chain contains a loop
    // e.g. parent->GetIndirectKey( "Parent" ) == parent or parent->GetIndirectKey( "Parent" )->GetIndirectKey( "Parent" ) == parent
    constexpr unsigned maxRecursionDepth = 1000;
    unsigned depth = 0;

    while (parent != nullptr)
    {
        auto kidsObj = parent->GetDictionary().FindKey("Kids");
        if (kidsObj != nullptr)
        {
            const PdfArray& kids = kidsObj->GetArray();
            for (auto& child : kids)
            {
                if (child.GetReference() == ref)
                    break;

                auto node = this->GetDocument().GetObjects().GetObject(child.GetReference());
                if (node == nullptr)
                {
                    PODOFO_RAISE_ERROR_INFO(PdfErrorCode::NoObject,
                        "Object {} not found from Kids array {}", child.GetReference().ToString(),
                        kidsObj->GetIndirectReference().ToString());
                }

                if (node->GetDictionary().HasKey(PdfName::KeyType)
                    && node->GetDictionary().MustFindKey(PdfName::KeyType).GetName() == "Pages")
                {
                    auto count = node->GetDictionary().FindKey("Count");
                    if (count != nullptr)
                        pageNumber += static_cast<int>(count->GetNumber());
                }
                else
                {
                    // if we do not have a page tree node, 
                    // we most likely have a page object:
                    // so the page count is 1
                    pageNumber++;
                }
            }
        }

        ref = parent->GetIndirectReference();
        parent = parent->GetDictionary().FindKey("Parent");
        depth++;

        if (depth > maxRecursionDepth)
            PODOFO_RAISE_ERROR_INFO(PdfErrorCode::BrokenFile, "Loop in Parent chain");
    }

    return ++pageNumber;
}

void PdfPage::SetICCProfile(const string_view& csTag, InputStream& stream,
    int64_t colorComponents, PdfColorSpaceType alternateColorSpace)
{
    // Check nColorComponents for a valid value
    if (colorComponents != 1 &&
        colorComponents != 3 &&
        colorComponents != 4)
    {
        PODOFO_RAISE_ERROR_INFO(PdfErrorCode::ValueOutOfRange, "SetICCProfile nColorComponents must be 1, 3 or 4!");
    }

    // Create a colorspace object
    auto& iccObject = this->GetDocument().GetObjects().CreateDictionaryObject();
    PdfName nameForCS = PoDoFo::ColorSpaceToNameRaw(alternateColorSpace);
    iccObject.GetDictionary().AddKey("Alternate", nameForCS);
    iccObject.GetDictionary().AddKey("N", colorComponents);
    iccObject.GetOrCreateStream().SetData(stream);

    // Add the colorspace
    PdfArray array;
    array.Add(PdfName("ICCBased"));
    array.Add(iccObject.GetIndirectReference());

    PdfDictionary iccBasedDictionary;
    iccBasedDictionary.AddKey(csTag, array);

    // Add the colorspace to resource
    GetOrCreateResources().GetDictionary().AddKey("ColorSpace", iccBasedDictionary);
}

PdfContents& PdfPage::GetOrCreateContents()
{
    ensureContentsCreated();
    return *m_Contents;
}

PdfResources* PdfPage::getResources()
{
    return m_Resources.get();
}

PdfObject* PdfPage::getContentsObject()
{
    if (m_Contents == nullptr)
        return nullptr;

    return &const_cast<PdfContents&>(*m_Contents).GetObject();
}

PdfElement& PdfPage::getElement()
{
    return const_cast<PdfPage&>(*this);
}

PdfResources& PdfPage::GetOrCreateResources()
{
    ensureResourcesCreated();
    return *m_Resources;
}

const PdfContents& PdfPage::MustGetContents() const
{
    if (m_Contents == nullptr)
        PODOFO_RAISE_ERROR(PdfErrorCode::InvalidHandle);

    return *m_Contents;
}

PdfContents& PdfPage::MustGetContents()
{
    if (m_Contents == nullptr)
        PODOFO_RAISE_ERROR(PdfErrorCode::InvalidHandle);

    return *m_Contents;
}

const PdfResources& PdfPage::MustGetResources() const
{
    if (m_Resources == nullptr)
        PODOFO_RAISE_ERROR(PdfErrorCode::InvalidHandle);

    return *m_Resources;
}

PdfResources& PdfPage::MustGetResources()
{
    if (m_Resources == nullptr)
        PODOFO_RAISE_ERROR(PdfErrorCode::InvalidHandle);

    return *m_Resources;
}

Rect PdfPage::GetMediaBox(bool raw) const
{
    return getPageBox("MediaBox", raw);
}

Rect PdfPage::GetCropBox(bool raw) const
{
    return getPageBox("CropBox", raw);
}

Rect PdfPage::GetTrimBox(bool raw) const
{
    return getPageBox("TrimBox", raw);
}

Rect PdfPage::GetBleedBox(bool raw) const
{
    return getPageBox("BleedBox", raw);
}

Rect PdfPage::GetArtBox(bool raw) const
{
    return getPageBox("ArtBox", raw);
}

// https://stackoverflow.com/a/2021986/213871
int normalize(int value, int start, int end)
{
    int width = end - start;
    int offsetValue = value - start;   // value relative to 0

    // + start to reset back to start of original range
    return offsetValue - (offsetValue / width) * width + start;
}

PdfResources* getResources(PdfObject& obj, const deque<PdfObject*>& listOfParents)
{
    auto resources = obj.GetDictionary().FindKey("Resources");
    if (resources == nullptr)
    {
        // Resources might be inherited
        for (auto& parent : listOfParents)
            resources = parent->GetDictionary().FindKey("Resources");
    }

    if (resources == nullptr)
        return nullptr;

    return new PdfResources(*resources);
}
