#!/usr/bin/env bash
# fuzz-tally.sh STATS_JSON
#
# Adds every completed ClusterFuzzLite batch run not yet in STATS_JSON
# (created if missing) to its running total of fuzz inputs executed. A
# run's count is the sum of libFuzzer's "stat::number_of_executed_units"
# lines across its job logs. Runs are recorded by id, so running this again
# never counts a run twice. The docs page shows the total.
#
# Needs GH_TOKEN (actions: read) and GITHUB_REPOSITORY; uses curl and jq.
set -euo pipefail

out=${1:?usage: fuzz-tally.sh STATS_JSON}
api="https://api.github.com/repos/${GITHUB_REPOSITORY:?}"
auth=(-H "Authorization: Bearer ${GH_TOKEN:?}" -H "Accept: application/vnd.github+json")

[ -s "$out" ] || echo '{"runs": {}}' > "$out"
runs=$(jq -c '.runs' "$out")
counted=$runs

# Completed batch runs, newest first. Once something is counted, only the
# last few days need listing: older runs were counted on earlier passes.
since=$(jq -r '[.runs[].created] | max // empty' "$out")
filter="status=completed&per_page=100"
if [ -n "$since" ]; then
    from=$(date -u -d "${since%%T*} -3 days" +%Y-%m-%d)
    filter="$filter&created=%3E%3D$from"
fi
page=1
while :; do
    resp=$(curl -fsSL "${auth[@]}" "$api/actions/runs?$filter&page=$page")
    while read -r id created; do
        [ -n "$id" ] || continue
        if jq -e --arg id "$id" 'has($id)' <<<"$runs" >/dev/null; then continue; fi
        n=0
        for job in $(curl -fsSL "${auth[@]}" "$api/actions/runs/$id/jobs" | jq -r '.jobs[].id'); do
            k=$(curl -fsSL "${auth[@]}" "$api/actions/jobs/$job/logs" \
                | grep -oE 'stat::number_of_executed_units: [0-9]+' \
                | awk '{ s += $2 } END { print s + 0 }')
            n=$((n + k))
        done
        echo "run $id ($created): $n executions"
        runs=$(jq -c --arg id "$id" --arg c "$created" --argjson n "$n" \
            '. + {($id): {executions: $n, created: $c}}' <<<"$runs")
    done < <(jq -r '.workflow_runs[]
                    | select(.name | startswith("ClusterFuzzLite batch fuzzing"))
                    | "\(.id) \(.created_at)"' <<<"$resp")
    [ "$(jq '.workflow_runs | length' <<<"$resp")" -eq 100 ] || break
    page=$((page + 1))
done

if [ "$runs" = "$counted" ]; then
    echo "no new runs"
    exit 0
fi
jq -n --argjson runs "$runs" --arg now "$(date -u +%Y-%m-%dT%H:%M:%SZ)" '{
    total_executions: ([$runs[].executions] | add // 0),
    runs_counted: ($runs | length),
    first_run: ([$runs[].created] | min),
    updated: $now,
    runs: $runs
}' > "$out"
jq -r '"total: \(.total_executions) executions over \(.runs_counted) runs"' "$out"
