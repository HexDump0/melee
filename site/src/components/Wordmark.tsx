/* The Melee Unbound lockup.

   Two files, one per surface: the white-on-dark mark and the light-surface
   variant for the pale footer band. Never recolour these with a CSS filter —
   branding.md is explicit about it, and the light variant is a real file.

   `width` is the SVG *box* width; the `.wordmark` class crops the clear space
   baked into the file so the mark can sit flush to a gutter. See the comment
   on `.wordmark` in styles.css. */

type Props = {
  /** Which surface it sits on. */
  surface: 'dark' | 'light';
  /** CSS length for the box width, e.g. `min(44.5rem, 78vw)`. */
  width: string;
  className?: string;
};

export function Wordmark({ surface, width, className = '' }: Props) {
  return (
    <img
      src={surface === 'dark' ? '/media/wordmark.svg' : '/media/wordmark-light.svg'}
      alt="Melee Unbound"
      width={1280}
      height={400}
      style={{ '--wordmark-w': width } as React.CSSProperties}
      className={`wordmark ${className}`}
    />
  );
}
