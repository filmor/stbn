#include <string>
#include <unordered_map>
#include <iostream>
#include <vector>

#include "mdbtools.h"
#include <emscripten/bind.h>

using namespace emscripten;

class MdbTable;

class Mdb {
public:
  Mdb(std::string const &path) {
    std::cout << "Reading file " << path << std::endl;
    auto handle = mdb_open(path.c_str(), MdbFileFlags::MDB_WRITABLE);
    if (handle == nullptr)
      throw "failed to open file";
    
    std::cout << "Reading catalog" << std::endl;
    if (!mdb_read_catalog(handle, MDB_TABLE))
    {
        mdb_close(handle);
        throw "failed to read catalog";
    }

    std::cout << "Done" << std::endl;

    m_handle = handle;
  }

  ~Mdb() {
    for (auto &k : m_tables) {
      mdb_free_tabledef(k.second);
    }

    mdb_close(m_handle);
  }

  std::vector<std::string> get_tables() {
      auto res = std::vector<std::string>();
      for (int i = 0; i < m_handle->num_catalog; ++i) {
		auto entry = (MdbCatalogEntry*)g_ptr_array_index (m_handle->catalog, i);

        res.emplace_back(entry->object_name);
      }
      return res;
  }

  bool load_table(std::string const &name) {
    if (m_tables.count(name))
      return true;

    std::cout << "Reading table " << name << std::endl;
    auto table =
        mdb_read_table_by_name(m_handle, (char *)name.c_str(), MDB_TABLE);
    if (table == nullptr)
      return false;

    m_tables[name] = table;

    std::cout << "Reading columns " << std::endl;
    mdb_read_columns(table);
    mdb_rewind_table(table);

    return true;
  }

private:
  MdbHandle *m_handle;
  std::unordered_map<std::string, MdbTableDef *> m_tables;
};

EMSCRIPTEN_BINDINGS(libmdb) {
  class_<Mdb>("Mdb")
    .constructor<std::string const &>()
    .function("load_table", &Mdb::load_table)
    .function("get_tables", &Mdb::get_tables)
    ;

  register_vector<std::string>("vector<string>");
  // function("mdbjs_open", &mdbjs_open);
}