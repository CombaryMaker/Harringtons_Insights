# offline.zeek - no online lookups: detect-MHR (Team Cymru Malware Hash Registry) checks no file types.
@load frameworks/files/detect-MHR
redef TeamCymruMalwareHashRegistry::match_file_types = /^NO-LOOKUPS$/;
