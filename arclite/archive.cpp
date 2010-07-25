#include "msg.h"
#include "utils.hpp"
#include "sysutils.hpp"
#include "farutils.hpp"
#include "common_types.hpp"
#include "ui.hpp"
#include "archive.hpp"

HRESULT ArcLib::get_bool_prop(UInt32 index, PROPID prop_id, bool& value) const {
  PropVariant prop;
  HRESULT res = GetHandlerProperty2(index, prop_id, &prop);
  if (FAILED(res))
    return res;
  if (prop.vt == VT_BOOL)
    value = prop.boolVal == VARIANT_TRUE;
  else
    return E_FAIL;
  return S_OK;
}

HRESULT ArcLib::get_string_prop(UInt32 index, PROPID prop_id, wstring& value) const {
  PropVariant prop;
  HRESULT res = GetHandlerProperty2(index, prop_id, &prop);
  if (FAILED(res))
    return res;
  if (prop.vt == VT_BSTR)
    value = prop.bstrVal;
  else
    return E_FAIL;
  return S_OK;
}

HRESULT ArcLib::get_bytes_prop(UInt32 index, PROPID prop_id, string& value) const {
  PropVariant prop;
  HRESULT res = GetHandlerProperty2(index, prop_id, &prop);
  if (FAILED(res))
    return res;
  if (prop.vt == VT_BSTR) {
    UINT len = SysStringByteLen(prop.bstrVal);
    value.assign(reinterpret_cast<string::const_pointer>(prop.bstrVal), len);
  }
  else
    return E_FAIL;
  return S_OK;
}

void ArcLibs::load(const wstring& path) {
  FileEnum file_enum(path);
  while (file_enum.next()) {
    ArcLib arc_lib;
    arc_lib.h_module = LoadLibraryW((add_trailing_slash(path) + file_enum.data().cFileName).c_str());
    if (arc_lib.h_module) {
      arc_lib.CreateObject = reinterpret_cast<ArcLib::FCreateObject>(GetProcAddress(arc_lib.h_module, "CreateObject"));
      arc_lib.GetNumberOfMethods = reinterpret_cast<ArcLib::FGetNumberOfMethods>(GetProcAddress(arc_lib.h_module, "GetNumberOfMethods"));
      arc_lib.GetMethodProperty = reinterpret_cast<ArcLib::FGetMethodProperty>(GetProcAddress(arc_lib.h_module, "GetMethodProperty"));
      arc_lib.GetNumberOfFormats = reinterpret_cast<ArcLib::FGetNumberOfFormats>(GetProcAddress(arc_lib.h_module, "GetNumberOfFormats"));
      arc_lib.GetHandlerProperty = reinterpret_cast<ArcLib::FGetHandlerProperty>(GetProcAddress(arc_lib.h_module, "GetHandlerProperty"));
      arc_lib.GetHandlerProperty2 = reinterpret_cast<ArcLib::FGetHandlerProperty2>(GetProcAddress(arc_lib.h_module, "GetHandlerProperty2"));
      if (arc_lib.CreateObject && arc_lib.GetNumberOfFormats && arc_lib.GetHandlerProperty2) {
        push_back(arc_lib);
      }
      else {
        FreeLibrary(arc_lib.h_module);
      }
    }
  }
}

ArcLibs::~ArcLibs() {
  for (const_iterator arc_lib = begin(); arc_lib != end(); arc_lib++) {
    FreeLibrary(arc_lib->h_module);
  }
  clear();
}

void ArcFormats::load(const ArcLibs& arc_libs) {
  for (ArcLibs::const_iterator arc_lib = arc_libs.begin(); arc_lib != arc_libs.end(); arc_lib++) {
    UInt32 num_formats;
    if (arc_lib->GetNumberOfFormats(&num_formats) == S_OK) {
      for (UInt32 idx = 0; idx < num_formats; idx++) {
        ArcFormat arc_format;
        arc_format.arc_lib = &*arc_lib;
        CHECK_COM(arc_lib->get_string_prop(idx, NArchive::kName, arc_format.name));
        CHECK_COM(arc_lib->get_bytes_prop(idx, NArchive::kClassID, arc_format.class_id));
        CHECK_COM(arc_lib->get_bool_prop(idx, NArchive::kUpdate, arc_format.update));
        arc_lib->get_bytes_prop(idx, NArchive::kStartSignature, arc_format.start_signature);
        arc_lib->get_string_prop(idx, NArchive::kExtension, arc_format.extension);
        push_back(arc_format);
      }
    }
  }
}

const ArcFormat* ArcFormats::find_by_name(const wstring& arc_name) const {
  for (const_iterator arc_format = begin(); arc_format != end(); arc_format++) {
    if (arc_format->name == arc_name)
      return &*arc_format;
  }
  return nullptr;
}


Archive::Archive(const ArcFormats& arc_formats): arc_formats(arc_formats) {
}

wstring Archive::get_default_name() const {
  wstring name = archive_file_info.cFileName;
  size_t pos = name.find_last_of(L'.');
  if (pos == wstring::npos)
    return name;
  else
    return name.substr(0, pos);
}

bool FileInfo::operator<(const FileInfo& file_info) const {
  if (parent == file_info.parent)
    if (is_dir() == file_info.is_dir())
      return lstrcmpiW(name.c_str(), file_info.name.c_str()) < 0;
    else
      return is_dir();
  else
    return parent < file_info.parent;
}

void Archive::make_index() {
  class Progress: public ProgressMonitor {
  private:
    UInt32 completed;
    UInt32 total;
    virtual void do_update_ui() {
      wostringstream st;
      st << Far::get_msg(MSG_PLUGIN_NAME) << L'\n';
      st << completed << L" / " << total << L'\n';
      st << Far::get_progress_bar_str(60, completed, total) << L'\n';
      Far::message(st.str(), 0, FMSG_LEFTALIGN);
    }
  public:
    Progress(): completed(0), total(0) {
    }
    void update(UInt32 completed, UInt32 total) {
      this->completed = completed;
      this->total = total;
      update_ui();
    }
  };
  Progress progress;

  num_indices = 0;
  CHECK_COM(in_arc->GetNumberOfItems(&num_indices));
  file_list.clear();
  file_list.reserve(num_indices);

  struct DirInfo {
    UInt32 index;
    UInt32 parent;
    wstring name;
    bool operator<(const DirInfo& dir_info) const {
      if (parent == dir_info.parent)
        return name < dir_info.name;
      else
        return parent < dir_info.parent;
    }
  };
  typedef set<DirInfo> DirList;
  map<UInt32, unsigned> dir_index_map;
  DirList dir_list;

  DirInfo dir_info;
  UInt32 dir_index = 0;
  FileInfo file_info;
  wstring path;
  PropVariant var;
  for (UInt32 i = 0; i < num_indices; i++) {
    progress.update(i, num_indices);

    if (s_ok(in_arc->GetProperty(i, kpidPath, &var)) && var.vt == VT_BSTR)
      path.assign(var.bstrVal);
    else
      path.assign(get_default_name());

    // attributes
    bool is_dir = s_ok(in_arc->GetProperty(i, kpidIsDir, &var)) && var.vt == VT_BOOL && var.boolVal;
    if (s_ok(in_arc->GetProperty(i, kpidAttrib, &var)) && var.vt == VT_UI4)
      file_info.attr = var.ulVal;
    else
      file_info.attr = 0;
    if (is_dir)
      file_info.attr |= FILE_ATTRIBUTE_DIRECTORY;
    else
      is_dir = file_info.is_dir();

    // size
    if (!is_dir && s_ok(in_arc->GetProperty(i, kpidSize, &var)) && var.vt == VT_UI8)
      file_info.size = var.uhVal.QuadPart;
    else
      file_info.size = 0;
    if (!is_dir && s_ok(in_arc->GetProperty(i, kpidPackSize, &var)) && var.vt == VT_UI8)
      file_info.psize = var.uhVal.QuadPart;
    else
      file_info.psize = 0;

    // date & time
    if (s_ok(in_arc->GetProperty(i, kpidCTime, &var)) && var.vt == VT_FILETIME)
      file_info.ctime = var.filetime;
    else
      file_info.ctime = archive_file_info.ftCreationTime;
    if (s_ok(in_arc->GetProperty(i, kpidMTime, &var)) && var.vt == VT_FILETIME)
      file_info.mtime = var.filetime;
    else
      file_info.mtime = archive_file_info.ftLastWriteTime;
    if (s_ok(in_arc->GetProperty(i, kpidATime, &var)) && var.vt == VT_FILETIME)
      file_info.atime = var.filetime;
    else
      file_info.atime = archive_file_info.ftLastAccessTime;

    // file name
    size_t name_end_pos = path.size();
    while (name_end_pos && is_slash(path[name_end_pos - 1])) name_end_pos--;
    size_t name_pos = name_end_pos;
    while (name_pos && !is_slash(path[name_pos - 1])) name_pos--;
    file_info.name.assign(path.data() + name_pos, name_end_pos - name_pos);

    // split path into individual directories and put them into DirList
    dir_info.parent = c_root_index;
    size_t begin_pos = 0;
    while (begin_pos < name_pos) {
      dir_info.index = dir_index;
      size_t end_pos = begin_pos;
      while (end_pos < name_pos && !is_slash(path[end_pos])) end_pos++;
      if (end_pos != begin_pos) {
        dir_info.name.assign(path.data() + begin_pos, end_pos - begin_pos);
        pair<DirList::iterator, bool> ins_pos = dir_list.insert(dir_info);
        if (ins_pos.second)
          dir_index++;
        dir_info.parent = ins_pos.first->index;
      }
      begin_pos = end_pos + 1;
    }
    file_info.parent = dir_info.parent;

    if (is_dir) {
      dir_info.index = dir_index;
      dir_info.parent = file_info.parent;
      dir_info.name = file_info.name;
      pair<DirList::iterator, bool> ins_pos = dir_list.insert(dir_info);
      if (ins_pos.second)
        dir_index++;
      dir_index_map[ins_pos.first->index] = i;
    }

    file_list.push_back(file_info);
  }

  // add directories that not present in archive index
  file_list.reserve(file_list.size() + dir_list.size() - dir_index_map.size());
  dir_index = num_indices;
  for_each(dir_list.begin(), dir_list.end(), [&] (const DirInfo& dir_info) {
    if (dir_index_map.count(dir_info.index) == 0) {
      dir_index_map[dir_info.index] = dir_index;
      file_info.parent = dir_info.parent;
      file_info.name = dir_info.name;
      file_info.attr = FILE_ATTRIBUTE_DIRECTORY;
      file_info.size = file_info.psize = 0;
      file_info.ctime = archive_file_info.ftCreationTime;
      file_info.mtime = archive_file_info.ftLastWriteTime;
      file_info.atime = archive_file_info.ftLastAccessTime;
      dir_index++;
      file_list.push_back(file_info);
    }
  });

  // fix parent references
  for_each(file_list.begin(), file_list.end(), [&] (FileInfo& file_info) {
    if (file_info.parent != c_root_index)
      file_info.parent = dir_index_map[file_info.parent];
  });

  // create search index
  file_list_index.clear();
  file_list_index.reserve(file_list.size());
  for (size_t i = 0; i < file_list.size(); i++) {
    file_list_index.push_back(i);
  }
  sort(file_list_index.begin(), file_list_index.end(), [&] (UInt32 left, UInt32 right) -> bool {
    return file_list[left] < file_list[right];
  });
}

UInt32 Archive::find_dir(const wstring& path) {
  if (file_list.empty())
    make_index();

  FileInfo dir_info;
  dir_info.attr = FILE_ATTRIBUTE_DIRECTORY;
  dir_info.parent = c_root_index;
  size_t begin_pos = 0;
  while (begin_pos < path.size()) {
    size_t end_pos = begin_pos;
    while (end_pos < path.size() && !is_slash(path[end_pos])) end_pos++;
    if (end_pos != begin_pos) {
      dir_info.name.assign(path.data() + begin_pos, end_pos - begin_pos);
      FileIndexRange fi_range = equal_range(file_list_index.begin(), file_list_index.end(), -1, [&] (UInt32 left, UInt32 right) -> bool {
        const FileInfo& fi_left = left == -1 ? dir_info : file_list[left];
        const FileInfo& fi_right = right == -1 ? dir_info : file_list[right];
        return fi_left < fi_right;
      });
      if (fi_range.first == fi_range.second)
        FAIL(ERROR_PATH_NOT_FOUND);
      dir_info.parent = *fi_range.first;
    }
    begin_pos = end_pos + 1;
  }
  return dir_info.parent;
}

FileIndexRange Archive::get_dir_list(UInt32 dir_index) {
  if (file_list.empty())
    make_index();

  FileInfo file_info;
  file_info.parent = dir_index;
  FileIndexRange index_range = equal_range(file_list_index.begin(), file_list_index.end(), -1, [&] (UInt32 left, UInt32 right) -> bool {
    const FileInfo& fi_left = left == -1 ? file_info : file_list[left];
    const FileInfo& fi_right = right == -1 ? file_info : file_list[right];
    return fi_left.parent < fi_right.parent;
  });

  return index_range;
}