// A module in the TypeScript the stripper reads (erasable syntax with
// verbatimModuleSyntax), many forms in one place: esbuild must make the same
// JavaScript of it as of what the stripper leaves.
import type { Signal } from "openmui";
import { signal, type Model, model as makeModel } from "openmui";
import * as all from "openmui";
export type { Signal };

type Row = { key: string; title: string; stars?: number };
type Keys<T> = T extends Record<infer K extends string, unknown> ? K : never;
type Mapped<T> = { readonly [K in keyof T]-?: T[K] extends Function ? never : T[K] };
type Tagged = `row-${number}` | "none";
type Fn = <T>(value: T, ...rest: readonly unknown[]) => asserts value is NonNullable<T>;
type Ctor = abstract new (...args: any[]) => object;
interface Store<T extends object = Row> extends Iterable<T> {
  get(key: string): T | undefined;
  readonly size: number;
  [name: string]: unknown;
}
declare global {
  interface Window { omui: unknown }
}
declare const BUILD: string;
declare function host(name: string): number;

export const rows: Row[] = [];
const count: Signal<number> = signal(0) as unknown as Signal<number>;
let pending!: Promise<void>;
let maybe: Row | null = null;

export function find<T extends Row>(list: readonly T[], key: string): T | undefined;
export function find(list: readonly Row[], key: number): Row | undefined;
export function find(list: readonly Row[], key: string | number): Row | undefined {
  return list.find((row: Row): boolean => row.key === String(key));
}

export default class Shelf<T extends Row = Row> implements Store<T> {
  static readonly kind: string = "shelf";
  #items = new Map<string, T>();
  declare readonly brand: unique symbol;
  protected label?: string;
  public size = 0;
  [name: string]: unknown;

  constructor(items: Iterable<T> = []) {
    for (const item of items) this.#items.set(item.key, item);
    this.size = this.#items.size;
  }

  get(key: string): T | undefined {
    return this.#items.get(key)!;
  }
  get first(): T | undefined {
    return this.#items.values().next().value as T | undefined;
  }
  *[Symbol.iterator](): Iterator<T> {
    yield* this.#items.values();
  }
  async load<U extends T>(this: Shelf<U>, source: () => Promise<U[]>): Promise<number> {
    const loaded = await source();
    return loaded.length satisfies number;
  }
  static of<R extends Row>(...rows: R[]): Shelf<R> {
    return new Shelf<R>(rows);
  }
}

abstract class Shape {
  abstract area(): number;
  protected abstract readonly sides: number;
  describe(): string {
    return `${this.constructor.name}: ${this.area().toFixed(2)}`;
  }
}

class Square extends Shape {
  protected readonly sides = 4;
  private side: number;
  constructor(side: number = 1) {
    super();
    this.side = side;
  }
  area(): number {
    return this.side ** 2;
  }
}

const pick = <K extends keyof Row,>(row: Row, key: K): Row[K] => row[key];
const twice = async <T,>(value: T): Promise<[T, T]> => [value, value];
const parse = (text: string): { ok: true; value: number } | { ok: false } =>
  Number.isNaN(Number(text)) ? { ok: false } : { ok: true, value: Number(text) };
const multi = (a: number,
               b: number): {
  sum: number;
} => ({ sum: a + b });
const chosen = maybe ? (maybe): string => maybe.title : (fallback: string) => fallback;
const plain = maybe ? (rows) : rows;
const generic = all.signal<number>(1);
const instantiated = Array<string>;
const compare = count.get() < 2 > false;
const shifted = (1 >> 2) >>> 1 >= 0;
const regex = /<(\w+)>[^/]*\/\1>/g;
const nested = `a ${`b ${pick(rows[0]!, "title") as string} c`} d`;
let index = 0;
index += rows.length as number
;[1, 2].forEach((value: number) => { index += value; });

label: for (const [key, value] of Object.entries(rows) as [string, Row][]) {
  if (value.stars! > 3) continue label;
  switch (key) {
    case "a": break;
    default: index--;
  }
}

try {
  host("x");
} catch (error: unknown) {
  console.log(error as Error);
}

export { pick, twice, parse, multi, chosen, plain, generic, instantiated, compare, shifted, regex, nested, type Row };
export const store: Model = makeModel("rows");
