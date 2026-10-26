import type { SeriesPoint } from '../lib/history';

interface Props {
  title: string;
  unit: string;
  points: SeriesPoint[];
  fromMs: number;
  toMs: number;
  decimals: number;
  /** Shaded target band, e.g. pH 7.5–8.5 */
  band?: { min: number; max: number };
  locale: string;
  emptyText: string;
}

const W = 320, H = 140, L = 34, R = 8, T = 8, B = 20;

/** Minimal SVG line chart. Gaps (null) break the line instead of interpolating. */
export function LineChart({ title, unit, points, fromMs, toMs, decimals, band, locale, emptyText }: Props) {
  const values = points.map((p) => p.v).filter((v): v is number => v !== null);
  const latest = [...points].reverse().find((p) => p.v !== null)?.v ?? null;
  if (!values.length) {
    return (
      <div className="panel chart">
        <h3>{title}</h3>
        <p className="muted small" style={{ padding: '40px 0' }}>{emptyText}</p>
      </div>
    );
  }
  let lo = Math.min(...values, band?.min ?? Infinity);
  let hi = Math.max(...values, band?.max ?? -Infinity);
  if (hi - lo < 1e-6) { lo -= 1; hi += 1; }
  const pad = (hi - lo) * 0.08;
  lo -= pad; hi += pad;
  const x = (t: number) => L + ((t - fromMs) / (toMs - fromMs)) * (W - L - R);
  const y = (v: number) => T + (1 - (v - lo) / (hi - lo)) * (H - T - B);

  const segments: string[] = [];
  let current = '';
  for (const p of points) {
    if (p.v === null) {
      if (current) segments.push(current);
      current = '';
      continue;
    }
    current += `${current ? 'L' : 'M'}${x(p.t).toFixed(1)},${y(p.v).toFixed(1)}`;
  }
  if (current) segments.push(current);

  const ticksY = [lo + pad, (lo + hi) / 2, hi - pad];
  const span = toMs - fromMs;
  const fmtTime = new Intl.DateTimeFormat(locale, span > 2 * 86400000 ? { weekday: 'short' } : { hour: '2-digit', minute: '2-digit' });
  const ticksX = [0, 0.25, 0.5, 0.75, 1].map((f) => fromMs + f * span);

  return (
    <div className="panel chart">
      <div className="row">
        <h3>{title}</h3>
        <span className="spacer" />
        <span className="small muted">
          {latest !== null ? `${latest.toFixed(decimals)} ${unit}` : ''}
        </span>
      </div>
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label={`${title}, ${unit}`} preserveAspectRatio="none">
        {band && (
          <rect className="band" x={L} width={W - L - R} y={y(Math.min(band.max, hi))} height={Math.max(0, y(Math.max(band.min, lo)) - y(Math.min(band.max, hi)))} />
        )}
        {ticksY.map((v) => (
          <g key={v}>
            <line className="grid-line" x1={L} x2={W - R} y1={y(v)} y2={y(v)} />
            <text className="axis" x={L - 4} y={y(v) + 3} textAnchor="end">{v.toFixed(decimals > 1 ? 1 : decimals)}</text>
          </g>
        ))}
        {ticksX.map((t, i) => (
          <text key={t} className="axis" x={x(t)} y={H - 4} textAnchor={i === 0 ? 'start' : i === 4 ? 'end' : 'middle'}>
            {fmtTime.format(t)}
          </text>
        ))}
        {segments.map((d) => (
          <path key={d} className="series" d={d} />
        ))}
      </svg>
    </div>
  );
}
