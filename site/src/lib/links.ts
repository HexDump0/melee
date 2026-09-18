/* Every off-site destination the page can point at.

   An empty string is meaningful: the page still renders the link and tells
   the visitor it is not set yet, rather than shipping a button that silently
   does nothing. See `useDestination`. */

export const LINKS = {
  repo: 'https://github.com/HexDump0/melee',
  releases: 'https://github.com/HexDump0/melee/releases',
  setup: 'https://github.com/HexDump0/melee#setup',
  mods: 'https://github.com/HexDump0/melee/tree/master/mods',
  issues: 'https://github.com/HexDump0/melee/issues',
  discord: 'https://discord.gg/87kwPPetPD',
} as const satisfies Record<string, string>;

export type LinkKey = keyof typeof LINKS;

/** What the toast calls a destination when it has no URL yet. */
export const LINK_LABELS: Record<LinkKey, string> = {
  repo: 'Repository',
  releases: 'Releases',
  setup: 'Setup guide',
  mods: 'Mods',
  issues: 'Issues',
  discord: 'Discord invite',
};
