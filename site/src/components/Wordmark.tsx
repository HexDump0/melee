/* The Melee Unbound lockup.

   Two files, one per surface: the white-on-dark mark and the light-surface
   variant for pale bands. Never recolour these with a CSS filter —
   branding.md is explicit about it, and the light variant is a real file.

   The files are cropped to the ink box (`viewBox="200 103 880 193"` of the
   original 1280x400), so the mark sits flush without the clear space the
   master files carry. Set the size with a `w-*` class from the caller. */

type Props = {
  /** Which surface it sits on. */
  surface: 'dark' | 'light';
  className?: string;
};

export function Wordmark({ surface, className }: Props) {
  return (
    <img
      src={surface === 'dark' ? '/media/wordmark.svg' : '/media/wordmark-light.svg'}
      alt="Melee Unbound"
      width={880}
      height={193}
      className={className}
    />
  );
}
