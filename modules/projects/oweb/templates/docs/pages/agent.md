# Agents

Every application reads to an agent as it reads to a person: the agent reads the semantic tree and performs the actions its nodes offer. There is no screenshot to interpret and no second interface to write.

## What an agent reads

| It asks for | It gets | Cost |
|:---|:---|---:|
| the tree | every node: role, name, value, states, actions | one request |
| a part of it | a subtree, by id and depth | one request |
| what changed | the difference since it last looked | one request |
| the pixels | any region, as the renderer drew it | when it must look |

## What an agent may do

An action says its effect before it is performed:

- [x] reading changes nothing and asks nothing
- [x] a reversible action is performed, and can be taken back
- [ ] an irreversible action needs the answer that confirms it
- [ ] an action that reaches outside the application needs the person's answer

The person decides how far an agent may go: in development everything declared is open; in a release, what the developer allows and the user's settings both permit.

Back to the [pages](index.md#pages).
