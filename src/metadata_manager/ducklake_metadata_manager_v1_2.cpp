#include "metadata_manager/ducklake_metadata_manager_v1_2.hpp"
#include "metadata_manager/sqlite_metadata_manager.hpp"
#include "metadata_manager/postgres_metadata_manager.hpp"
#include "metadata_manager/quack_metadata_manager.hpp"
#include "common/ducklake_version.hpp"

namespace duckdb {

template <typename Base>
string DuckLakeMetadataManagerV1_2<Base>::GetSnapshotChangesTableStatement() {
	return "CREATE TABLE {METADATA_CATALOG}.ducklake_snapshot_changes(snapshot_id BIGINT PRIMARY KEY, "
	       "changes_made VARCHAR, author VARCHAR, commit_message VARCHAR, commit_extra_info VARCHAR, "
	       "data_changes VARCHAR);";
}

template <typename Base>
string DuckLakeMetadataManagerV1_2<Base>::GetVersionString() {
	constexpr auto VERSION = DuckLakeVersion::V1_2_DEV_1;
	return DuckLakeVersionToString(VERSION);
}

template class DuckLakeMetadataManagerV1_2<DuckLakeMetadataManager>;
template class DuckLakeMetadataManagerV1_2<SQLiteMetadataManager>;
template class DuckLakeMetadataManagerV1_2<PostgresMetadataManager>;
template class DuckLakeMetadataManagerV1_2<QuackMetadataManager>;

} // namespace duckdb
