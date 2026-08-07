# encoding: utf-8
# msvc_arm64ec.py -- teach waf's msvc tool about ARM64/ARM64EC targets
# Copyright (C) 2026 stephen-cusi

def configure(conf):
	# waf's all_msvc_platforms only knows x86/x64 hosts; add native arm64,
	# arm64ec, arm64->arm64ec and amd64->arm64ec mappings so MSVC_TARGETS
	# can name them (vcvarsall accepts arm64/arm64ec/amd64_arm64ec).
	added = 0
	try:
		from waflib.Tools import msvc
		extra = [
			('arm64', 'arm64'),
			('arm64ec', 'arm64ec'),
			('arm64_arm64ec', 'arm64ec'),
			('amd64_arm64ec', 'arm64ec'),
		]
		for entry in extra:
			if entry not in msvc.all_msvc_platforms:
				msvc.all_msvc_platforms.append(entry)
				added += 1
	except Exception as err:
		conf.to_log('msvc_arm64ec: patch failed: %r' % (err,))
	conf.to_log('msvc_arm64ec: added %d arm64/arm64ec targets' % added)
