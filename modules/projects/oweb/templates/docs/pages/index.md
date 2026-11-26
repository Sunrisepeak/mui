# The minimal documents

A small site of documents, built by [oweb](reference/ops.md "The operations of the oweb command"): every page here is one semantic tree, written as HTML for a reader and as Markdown for an agent.

This set is deliberately *small* and **complete**: it holds every kind of content a documentation site has to carry, so one corpus checks the writings, the search and the questions an agent is asked. ~~It is not the user documentation.~~\
Each page is also plain text: add `index.md` to its address.

## Pages

1. [Start](start.md): from installing the tools to a first window.
2. [Interfaces](ui.md): one tree, three ways to write it.
   - code in three languages, and one block with none
   - a picture and a quotation
3. [Agents](agent.md): what an agent reads and what it may do.
4. [The command's operations](reference/ops.md), generated from its catalog.

In Chinese: [中文](zh/index.md).

## How to read this site

### As a person

Open a page. It reads before any script runs: headings, links, tables and code are HTML's own.

### As an agent

Fetch `llms.txt`, then a page's `index.md`; `site.search.xdef.json` holds every section's text. See [what an agent may do](agent.md#what-an-agent-may-do).

### As a search engine

`sitemap.xml` lists the routes; each page's `<title>` is its first heading.
