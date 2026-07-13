"""Render an offline release review using only fields emitted by the gate."""
from html import escape

STYLE = """
*{box-sizing:border-box}body{margin:0;background:#f2f3ee;color:#192b29;font:16px/1.5 system-ui,sans-serif}
main{max-width:1050px;margin:auto;padding:40px 24px 64px}header{border-bottom:3px solid #193833}
.brand{font-weight:720;letter-spacing:.02em}.eyebrow,th,summary{font:700 .73rem/1.5 ui-monospace,monospace;letter-spacing:.065em;text-transform:uppercase}
h1{font-size:clamp(36px,6vw,64px);line-height:1.05;letter-spacing:-.055em;margin:28px 0 10px}header p{max-width:690px;margin-bottom:26px}
.cards{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin:24px 0}
.card,section{background:#fff;border:1px solid #cdd6cf;padding:20px}.card strong{display:block;font-size:30px;letter-spacing:-.045em}
section{margin:16px 0}h2{font-size:24px;letter-spacing:-.035em;margin:4px 0}.status{display:inline-block;padding:3px 9px;font:700 .76rem/1.5 ui-monospace,monospace;border-radius:3px}
.ROLLBACK{color:#7f231b;background:#ffe7dc}.HOLD{color:#664200;background:#ffedb5}.PROMOTE{color:#0e4b38;background:#dcf1de}
table{width:100%;border-collapse:collapse;font-size:.9rem;margin:15px 0}td,th{text-align:left;padding:8px 6px;border-bottom:1px solid #dce2da}th{color:#42534e}
.scroll{overflow-x:auto}summary{cursor:pointer}footer{font-size:.88rem;color:#4b5d57}code{font-family:ui-monospace,monospace}
@media(max-width:620px){main{padding:24px 12px}.cards{grid-template-columns:1fr}.card{padding:12px}section{padding:14px}}
"""


def percent(bps):
    return "n/a" if bps is None else f"{bps / 100:.2f}%"


def badge(decision):
    label = escape(decision)
    return f'<span class="status {label}">{label}</span>'


def panel(name, data, title):
    rows = []
    details = []
    for route in data["routes"]:
        b, c = route["baseline"], route["candidate"]
        rows.append(f"<tr><td>{escape(route['route'])}</td><td>{c['requests']:,}</td>"
                    f"<td>{percent(b['error_bps'])} → {percent(c['error_bps'])}</td>"
                    f"<td>{percent(b['slow_bps'])} → {percent(c['slow_bps'])}</td>"
                    f"<td>{badge(route['decision'])}</td></tr>")
        if route["reasons"]:
            reasons = ", ".join(escape(r.replace("_", " ")) for r in route["reasons"])
            details.append(f"<li><strong>{escape(route['route'])}</strong> · {reasons}</li>")
    evidence = "<ul>" + "".join(details) + "</ul>" if details else "<p>Every observed route met policy.</p>"
    return (f"<section><div class='eyebrow'>{escape(name)}.csv</div><h2>{title}</h2>"
            f"{badge(data['decision'])}<div class='scroll'><table><thead><tr><th>Route</th>"
            "<th>Candidate requests</th><th>Error rate¹</th><th>Slow rate¹</th><th>Decision</th></tr>"
            f"</thead><tbody>{''.join(rows)}</tbody></table></div>"
            f"<details open><summary>Decision reasons</summary>{evidence}</details></section>")


def render_review(reports):
    regression = reports["regression"]
    candidates = [r["candidate"] for r in regression["routes"]]
    site_errors = sum(c["errors"] for c in candidates)
    site_volume = sum(c["requests"] for c in candidates)
    checkout = next(r["candidate"] for r in regression["routes"] if r["route"] == "checkout")
    policy = regression["policy"]
    panels = [panel("regression", regression, "Checkout regression"),
              panel("incomplete", reports["incomplete"], "Partial observation window"),
              panel("healthy", reports["healthy"], "Healthy rollout")]
    return f"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>Release review | CanaryLedger</title>
<style>{STYLE}</style></head><body><main><header><div class="brand">CanaryLedger</div>
<h1>Checkout release review</h1><p>Synthetic canary replay. The busiest route stayed healthy while checkout crossed both the error and latency limits. The gate evaluated each route before issuing its rollout decision.</p></header>
<div class="cards"><div class="card"><span class="eyebrow">Site-wide errors</span>
<strong>{100 * site_errors / site_volume:.2f}%</strong>{site_errors:,} of {site_volume:,} candidate requests</div>
<div class="card"><span class="eyebrow">Checkout errors</span><strong>{percent(checkout['error_bps'])}</strong>
{checkout['errors']} of {checkout['requests']:,} candidate requests</div><div class="card"><span class="eyebrow">Checkout slow requests</span>
<strong>{percent(checkout['slow_bps'])}</strong>{checkout['slow']} requests exceeded 250 ms</div></div>
{''.join(panels)}<footer>¹ Baseline → candidate. A slow request took more than 250 ms.
The policy requires {policy['minimum_requests']} requests per cohort. Absolute limits are {percent(policy['error_limit_bps'])} errors
and {percent(policy['slow_limit_bps'])} slow requests. Allowed increases are {policy['error_increase_bps']} and
{policy['slow_increase_bps']} basis points respectively. All rates use request-weighted counts.</footer></main></body></html>"""
