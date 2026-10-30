# Boot state and rule tables: compare an oracle run with a native run
#   harness/run_oracle.sh harness/scripts/boot_rules.cmd OUT_O; opensmac_headless harness/scripts/boot_rules.cmd --outdir OUT_N
#   compare.py OUT_O OUT_N; diff OUT_O/rules.txt OUT_N/rules.txt (strip CRs of the oracle file)
seed 1 1
snapshot turn_000
dumprules rules.txt
quit
