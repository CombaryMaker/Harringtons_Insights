# hs_shard.zeek - loaded in every Harrington Sets shard run of Zeek. Michael Hugh Harrington, 2026. MIT License.
# software.log and known_services.log are decided from per-host tables that a host-pair split divides between shards.
# Each shard therefore records every raw input to those decisions, with the network time it happened at; the merge
# step (hs_zeek_merge.py) replays Zeek's own rules over all shards' inputs in time order, as one process would.
module HSRaw;
export {
	redef enum Log::ID += { SW_LOG, KS_LOG };
	type SwRaw: record { nt: time &log; force_log: bool &log; info: Software::Info &log; };
	type KsRaw: record { nt: time &log; n: count &log; info: Known::ServicesInfo &log; };   # n = number of services (tells {} from {""})
}
event zeek_init()
	{
	Log::create_stream(SW_LOG, Log::Stream($columns=SwRaw, $path="hs_software_raw"));
	Log::create_stream(KS_LOG, Log::Stream($columns=KsRaw, $path="hs_known_services_raw"));
	}
event Software::register(info: Software::Info) &priority=100
	{ Log::write(SW_LOG, SwRaw($nt=network_time(), $force_log=info$force_log, $info=info)); }
module Known;   # service_info_commit is private to module Known, so its extra handler is declared inside that module
event service_info_commit(info: ServicesInfo) &priority=100   # every commit, before Zeek checks its per-server table
	{ Log::write(HSRaw::KS_LOG, HSRaw::KsRaw($nt=network_time(), $n=|info$service|, $info=info)); }
