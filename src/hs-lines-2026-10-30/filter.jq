if .__hsw then . else {id, src, v: ((.f1 * 3 + .f2 * 2 + .f3) | . * 100 | round), tag: (if .src == "app" then (.msg | split(" ") | length) else (.host // "none") end)} end
