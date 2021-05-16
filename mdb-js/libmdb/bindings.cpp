#include <iostream>
#include <mdbfakeglib.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <charconv>

#include "mdbtools.h"
#include <emscripten/bind.h>

using namespace emscripten;

class MdbTable;

static void set_value(val&, MdbColumn*, std::vector<char> const&, unsigned);

class Mdb {
public:
  Mdb(std::string const &path) : m_path(path) {
    reopen();
  }

  ~Mdb() {
    close();
  }

  val get_tables() {
    auto res = val::array();
    for (int i = 0; i < m_handle->num_catalog; ++i) {
      auto entry = (MdbCatalogEntry *)g_ptr_array_index(m_handle->catalog, i);
      if (mdb_is_user_table(entry))
        res.call<void>("push", std::string(entry->object_name));
    }
    return res;
  }

  bool load_table(std::string const &name) {
    if (m_tables.count(name) > 0)
      return true;

    std::cout << "Reading table " << name << std::endl;
    auto table =
        mdb_read_table_by_name(m_handle, (char *)name.c_str(), MDB_TABLE);
    if (table == nullptr)
      return false;

    m_tables[name] = table;

    mdb_read_columns(table);
    mdb_read_indices(table);

    mdb_rewind_table(table);

    return true;
  }

  val read_table(std::string const& name) {
    const unsigned EXPORT_BIND_SIZE = 200000;

    if (!load_table(name))
      throw "couldn't load table";
    
    auto table = m_tables[name];

    auto bound_values = std::vector<std::vector<char>>(table->num_cols);
    auto bound_lens = std::vector<int>(table->num_cols);

    for (int i = 0; i < table->num_cols; ++i) {
      bound_values[i].resize(EXPORT_BIND_SIZE);
      int ret = mdb_bind_column(table, i + 1, &(bound_values[i])[0], &bound_lens[i]);
      if (ret == -1) {
        throw "failed to bind column";
      }
    }

    auto res = val::array();

    mdb_rewind_table(table);
    while (mdb_fetch_row(table)) {
      auto row = val::object();

      for (int i = 0; i < table->num_cols; ++i) {
        auto col = (MdbColumn*)g_ptr_array_index(table->columns, i);

        if (bound_lens[i]) {
          if (col->col_type == MDB_OLE)
            throw "invalid col type";
          
          set_value(row, col, bound_values[i], bound_lens[i]);
        }
      }

      res.call<void>("push", row);
    }
    
    return res;
  }

  bool write_row(std::string const& name, val const& obj) {
    if (!load_table(name))
      throw "couldn't load table";
    
    auto table = m_tables[name];
    mdb_rewind_table(table);

    // TODO: Find and overwrite existing row

    auto fields = std::vector<MdbField>(table->num_cols);

    for (int i = 0; i < table->num_cols; ++i) {
      auto& field = fields[i];
      auto col = (MdbColumn*)g_ptr_array_index(table->columns, i);

      field.colnum = i;
      field.is_fixed = col->is_fixed;

      switch (col->col_type) {
        case MDB_TEXT:
        {
          auto s = obj[col->name].as<std::string>();
          std::cout << "Reading text " << col->name << " as " << s << std::endl;
          auto outlen = s.length() * 2;
          auto buf = (char*)g_malloc0(outlen);
          field.siz = mdb_ascii2unicode(m_handle, s.c_str(), s.length(), buf, outlen);
          field.value = buf;
          break;
        }
        case MDB_INT:
        {
          auto i16 = obj[col->name].as<int16_t>();
          std::cout << "Reading int " << col->name << " as " << i16 << " size " << mdb_col_fixed_size(col) << std::endl;
          field.siz = mdb_col_fixed_size(col);
          field.value = g_malloc0(field.siz);
          mdb_put_int16(field.value, 0, i16);
          break;
        }
        case MDB_LONGINT:
        {
          auto i32 = obj[col->name].as<uint32_t>();
          std::cout << "Reading long int " << col->name << " as " << i32 << " size " << mdb_col_fixed_size(col) << std::endl;
          field.siz = mdb_col_fixed_size(col);
          field.value = g_malloc0(field.siz);
          mdb_put_int32(field.value, 0, i32);
          break;
        }
        case MDB_BOOL:
        {
          auto b = obj[col->name].as<bool>();
          field.is_null = b ? 1 : 0;
          break;
        }
        case MDB_FLOAT:
        case MDB_DOUBLE:
        case MDB_MONEY:
        case MDB_DATETIME:
        case MDB_OLE:
        case MDB_MEMO:
        case MDB_REPID:
        case MDB_NUMERIC:
        default:
          throw "Invalid type";
      }
    }

    mdb_insert_row(table, table->num_cols, &fields[0]);

    // TODO: Clear memory allocated for the fields

    return true;
  }

  void save() {
    reopen();
  }

private:
  void reopen() {
    if (m_handle != nullptr)
      close();
    
    std::cout << "Reading file " << m_path << std::endl;
    auto handle = mdb_open(m_path.c_str(), MdbFileFlags::MDB_WRITABLE);
    if (handle == nullptr)
      throw "failed to open file";

    std::cout << "Reading catalog" << std::endl;
    if (!mdb_read_catalog(handle, MDB_TABLE)) {
      mdb_close(handle);
      throw "failed to read catalog";
    }

    m_handle = handle;
  }

  void close() {
    for (auto &k : m_tables) {
      mdb_free_tabledef(k.second);
    }
    m_tables.clear();

    mdb_close(m_handle);
  }

  std::string m_path;
  MdbHandle *m_handle;
  std::unordered_map<std::string, MdbTableDef *> m_tables;
};


static void set_value(val& row, MdbColumn* col, std::vector<char> const& value, unsigned size) {
  switch (col->col_type) {
    case MDB_TEXT:
    case MDB_BINARY:
    case MDB_DATETIME:
    case MDB_MEMO:
      row.set(col->name, std::string(value.data(), size));
      break;
    
    case MDB_BOOL:
      row.set(col->name, value[0] == '1');
      break;
    
    case MDB_BYTE:
    case MDB_INT:
    case MDB_LONGINT:
      long res;
      std::from_chars(&value[0], &value[0] + size, res, 10);
      row.set(col->name, res);
      break;
    
    default:
      std::cerr << "Unsupported type: " << col->col_type << std::endl;
  }
}


EMSCRIPTEN_BINDINGS(libmdb) {
  class_<Mdb>("Mdb")
      .constructor<std::string const &>()
      .function("load_table", &Mdb::load_table)
      .function("get_tables", &Mdb::get_tables)
      .function("read_table", &Mdb::read_table)
      .function("write_row", &Mdb::write_row)
      .function("save", &Mdb::save)
      ;

  register_vector<std::string>("vector<string>");
}