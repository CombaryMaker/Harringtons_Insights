ips = { variables = default_variables, enable_builtin_rules = true, include = '$SEC/rules/snort3-community-rules/snort3-community.rules' }
alert_csv = { file = true, fields = 'timestamp gid sid rev action proto src_addr src_port dst_addr dst_port msg' }
