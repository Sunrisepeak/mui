// The reading list: books and how much you liked them, the average, the ones
// you would recommend. `rating` is this application's own component
// (contract/rating.contract.omui.toml, src/rating.cpp): once the program
// registers it, its builder is in "openmui" like any other component's —
// nothing between C++ and this file is written by hand.
import { signal, fmt, model, verb, column, row, text, textBox, button, toggle, each, or, not, ge, eq, mount, rating } from "openmui";

type Book = { key: string; title: string; author: string; stars: number };

const books = model("books");
books.push([
  { key: "b1", title: "The Left Hand of Darkness", author: "Ursula K. Le Guin", stars: 5 },
  { key: "b2", title: "Middlemarch", author: "George Eliot", stars: 4.5 },
  { key: "b3", title: "The Name of the Rose", author: "Umberto Eco", stars: 4 },
  { key: "b4", title: "Infinite Jest", author: "David Foster Wallace", stars: 0 },
]);

const average = signal(0);
const rated = signal(0);
const total = signal(0);
const favourites = signal(false);
const draft = signal("");
let next = 5;

// The summary, from the rows as they are now.
function recount() {
  const rows: Book[] = books.rows();
  const given = rows.filter((b) => b.stars > 0);
  total.set(rows.length);
  rated.set(given.length);
  average.set(given.length ? Math.round((given.reduce((s, b) => s + b.stars, 0) / given.length) * 10) / 10 : 0);
}

function rate(key: string, stars: number): number {
  const rows: Book[] = books.rows();
  const i = rows.findIndex((b) => b.key === key);
  if (i < 0) throw new Error(`no book is ${key}`);
  books.splice(i, 1, [{ ...rows[i], stars }]);
  recount();
  return stars;
}

// What an agent does with the list: rate a book by its key.
verb("books.rate", { summary: "Rate a book (0 to 5, in half stars)", effect: "reversible" }, (a: { key: string; stars: number }) =>
  rate(a.key, Math.round(Math.min(Math.max(a.stars, 0), 5) * 2) / 2));

recount();

export const Shelf = () =>
  column({ padding: 20, gap: 14 }, [
    row({ id: "summary", gap: 12, alignItems: "center" }, [
      text({ id: "average", text: fmt("{} average", average), style: "heading" }),
      rating({ id: "overall", value: average, half: true, readonly: true, label: "Average rating" }),
      text({ id: "rated", text: fmt("{} of {} rated", rated, total), style: "caption" }),
    ]),
    toggle({ id: "favourites", label: "Four stars and up", checked: favourites }),
    column({ id: "books", gap: 10 }, [
      each("books",
        (book) => row({ gap: 12, alignItems: "center" }, [
          column({ gap: 2, grow: 1 }, [text({ text: book.title, style: "label" }), text({ text: book.author, style: "caption" })]),
          rating({ id: "stars", value: book.stars, half: true, label: book.title,
                   onChanged: (e: { value: number; rows: { row: Book } }) => { rate(e.rows.row.key, e.value); } }),
        ]),
        (book) => or(not(favourites), ge(book.stars, 4))),
    ]),
    row({ id: "add", gap: 8 }, [
      textBox({ id: "title", label: "Title", placeholder: "A book you read", text: draft, grow: 1 }),
      button({ id: "add", text: "Add", disabled: eq(draft, ""), onActivated: () => {
        books.push([{ key: `b${next++}`, title: draft.get(), author: "", stars: 0 }]);
        draft.set("");
        recount();
      } }),
    ]),
  ]);

mount("main", Shelf());
