#include "functions/ducklake_table_functions.hpp"
#include "storage/ducklake_catalog.hpp"
#include "storage/ducklake_table_entry.hpp"
#include "storage/ducklake_transaction.hpp"
#include "common/ducklake_util.hpp"
#include "storage/ducklake_transaction_changes.hpp"
#include "duckdb/planner/tableref/bound_at_clause.hpp"

namespace duckdb {

struct DuckLakeListFilesBindData : public TableFunctionData {
	vector<DuckLakeFileListEntry> files;
};

struct DuckLakeListFilesState : public GlobalTableFunctionState {
	vector<column_t> column_ids;
	idx_t offset = 0;
};

static Value GetFileValue(const DuckLakeFileData &file, idx_t column_id) {
	if (file.path.empty()) {
		return Value();
	}
	switch (column_id) {
	case 0:
		return Value(file.path);
	case 1:
		return Value::UBIGINT(file.file_size_bytes);
	case 2:
		return file.footer_size.IsValid() ? Value::UBIGINT(file.footer_size.GetIndex()) : Value();
	case 3:
		return file.encryption_key.empty() ? Value() : Value::BLOB_RAW(file.encryption_key);
	default:
		throw InternalException("Invalid file listing column %llu", column_id);
	}
}

static unique_ptr<FunctionData> DuckLakeListFilesBind(ClientContext &context, TableFunctionBindInput &input,
                                                      vector<LogicalType> &return_types, vector<Identifier> &names) {
	auto &catalog = DuckLakeBaseMetadataFunction::GetCatalog(context, input);
	auto &transaction = DuckLakeTransaction::Get(context, catalog);

	names.emplace_back("data_file");
	return_types.emplace_back(LogicalType::VARCHAR);

	names.emplace_back("data_file_size_bytes");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("data_file_footer_size");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("data_file_encryption_key");
	return_types.emplace_back(LogicalType::BLOB);

	names.emplace_back("delete_file");
	return_types.emplace_back(LogicalType::VARCHAR);

	names.emplace_back("delete_file_size_bytes");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("delete_file_footer_size");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("delete_file_encryption_key");
	return_types.emplace_back(LogicalType::BLOB);

	auto schema = DuckLakeTableFunctionUtil::GetStringOption(input, "schema");
	// generate the AT clause for the given list of parameters
	unique_ptr<BoundAtClause> at_clause;
	Value snapshot_version;
	if (DuckLakeTableFunctionUtil::TryGetNonNullOption(input, "snapshot_version", "snapshot id or tag name",
	                                                   snapshot_version)) {
		if (snapshot_version.type().id() == LogicalTypeId::TIMESTAMP_TZ) {
			throw BinderException("The snapshot_version option must be a snapshot id or a tag name, snapshot_time "
			                      "takes a timestamp");
		}
		at_clause = make_uniq<BoundAtClause>(DuckLakeTableFunctionUtil::VersionAtClauseFromValue(snapshot_version));
	}
	Value snapshot_time;
	if (DuckLakeTableFunctionUtil::TryGetNonNullOption(input, "snapshot_time", "timestamp", snapshot_time)) {
		if (at_clause) {
			throw InvalidInputException("Either snapshot_version OR snapshot_time must be specified - not both");
		}
		at_clause = make_uniq<BoundAtClause>(DuckLakeTableFunctionUtil::AtClauseFromValue(snapshot_time));
	}
	auto table_name = DuckLakeTableFunctionUtil::GetTableName(input.inputs[1]);
	EntryLookupInfo table_lookup(CatalogType::TABLE_ENTRY, QualifiedName(Identifier(table_name)), at_clause.get(),
	                             QueryErrorContext());
	auto &ducklake_table = DuckLakeBaseMetadataFunction::GetTableEntry(context, catalog, schema, table_lookup);
	auto snapshot = transaction.GetSnapshot(at_clause.get());

	// fetch the file list
	auto &metadata_manager = transaction.GetMetadataManager();
	// FIXME: support predicate pushdown
	// only the files are listed, not the columns of the inlined rows
	vector<DuckLakeProjectedColumn> no_columns;
	auto result = make_uniq<DuckLakeListFilesBindData>();
	result->files = metadata_manager.GetFilesForTable(ducklake_table, snapshot, nullptr, true, no_columns);
	for (auto &file : result->files) {
		file.inlined_file_deletions.clear();
	}
	return std::move(result);
}

static unique_ptr<GlobalTableFunctionState> DuckLakeListFilesInit(ClientContext &context,
                                                                  TableFunctionInitInput &input) {
	auto result = make_uniq<DuckLakeListFilesState>();
	result->column_ids = input.column_ids;
	return std::move(result);
}

static void DuckLakeListFilesExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &data = data_p.bind_data->Cast<DuckLakeListFilesBindData>();
	auto &state = data_p.global_state->Cast<DuckLakeListFilesState>();
	auto count = MinValue<idx_t>(STANDARD_VECTOR_SIZE, data.files.size() - state.offset);
	for (idx_t col_idx = 0; col_idx < output.ColumnCount(); col_idx++) {
		auto column_id = state.column_ids[col_idx];
		for (idx_t row_idx = 0; row_idx < count; row_idx++) {
			auto &entry = data.files[state.offset + row_idx];
			auto &file = column_id < 4 ? entry.file : entry.delete_file;
			output.data[col_idx].Append(GetFileValue(file, column_id % 4));
		}
	}
	state.offset += count;
	output.SetChildCardinality(count);
}

DuckLakeListFilesFunction::DuckLakeListFilesFunction()
    : DuckLakeBaseMetadataFunction("ducklake_list_files", DuckLakeListFilesBind) {
	function = DuckLakeListFilesExecute;
	init_global = DuckLakeListFilesInit;
	projection_pushdown = true;
	GetSignature()
	    .AddPositionalOnly("table_name", LogicalType::VARCHAR)
	    .WithTypedKwargs("options", [&](TypedKwargs &options) {
		    options.Add("schema", LogicalType::VARCHAR)
		        .Add("snapshot_version", LogicalType::ANY)
		        .Add("snapshot_time", LogicalType::TIMESTAMP_TZ);
	    });
}

} // namespace duckdb
