// What Kiln's caverns share: the legend (a character a cell) and a painter — rectangles of a character on a grid
// of air, read as rows. Not a data module itself: the levels (`*.world.omui.ts`) import it.

/** Each character: a material of the standard definition, or a role Kiln gives it. */
export const legend: { [character: string]: string } = {
  "#": "stone", w: "wood", "~": "water", s: "sand", o: "oil", l: "lava", g: "gunpowder", i: "iron", I: "ice",
  C: "body:wood", // wood that is a body: a crate (each connected piece one body)
  P: "start", // where Mui stands: her feet on the top of this cell
  E: "exit", // the region the exits span
};

/** A grid of air `width` × `height` to paint rectangles of a character on, read back as rows. */
export function painter(width: number, height: number) {
  const grid: string[][] = Array.from({ length: height }, () => Array.from({ length: width }, () => "."));
  const made = {
    /** Cells from (x0, y0) to (x1, y1), both included, the first row the top. */
    paint(x0: number, y0: number, x1: number, y1: number, what: string) {
      for (let y = y0; y <= y1; ++y) for (let x = x0; x <= x1; ++x) grid[y][x] = what;
      return made;
    },
    rows(): string[] {
      return grid.map((row) => row.join(""));
    },
  };
  return made;
}

/** The first cavern's rock, ledges, pool, wall, lava and exit; `flawed`: a second wall past the lava. */
export function firstCavern(flawed: boolean): string[] {
  const cave = painter(128, 64)
    .paint(0, 0, 127, 17, "#") // the rock above
    .paint(0, 62, 127, 63, "#") // and below
    .paint(0, 0, 1, 63, "#")
    .paint(126, 0, 127, 63, "#")
    .paint(2, 30, 29, 61, "#") // the ledge she starts on
    .paint(25, 21, 27, 29, "w") // a barricade of wood across the way: fire
    .paint(30, 56, 59, 61, "#") // the pool's floor
    .paint(30, 40, 59, 55, "~") // its water
    .paint(41, 34, 46, 39, "C") // a crate afloat
    .paint(60, 34, 79, 61, "#") // the far ledge
    .paint(76, 18, 79, 33, "#") // a wall to the ceiling: a bomb
    .paint(72, 31, 73, 33, "g") // a keg of gunpowder by it (fire at it: a hazard, not a key)
    .paint(80, 34, 85, 61, "#") // beyond the wall
    .paint(86, 44, 111, 61, "#") // the lava pit's floor
    .paint(86, 36, 111, 43, "l") // lava: water cools it to stone
    .paint(112, 34, 125, 61, "#") // the last floor
    .paint(118, 29, 121, 33, "E") // the exit
    .paint(22, 0, 23, 17, ".") // a shaft to the sky before the barricade: where a fire's heat leaves
    .paint(70, 0, 71, 17, "."); // and one before the wall
  if (flawed) cave.paint(113, 18, 116, 33, "#"); // the mistake: a second wall
  cave.paint(6, 30, 6, 30, "P").paint(6, 29, 6, 29, ".");
  return cave.rows();
}
