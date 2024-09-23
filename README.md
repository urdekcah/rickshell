# Rickshell

<p align="center">
  <a href="#why-build-rickshell">Why?</a> |
  <a href="#goals">Goals</a> |
  <a href="#project-status">Status</a> |
  <a href="#development-status">Development Status</a> |
  <a href="#license">License</a>
</p>

Rickshell was not designed to be a general-purpose tool or an ideal solution for a wide audience of developers. Instead, it started out as—and continues to be—a **personal project** for the developer known as **rickroot**(_real name_: **Kim Hyunseo**, GitHub: [@rickroot30](https://github.com/rickroot30)). Rickshell is a personal project that was developed to bring together tools, streamline workflows, and manage tasks in a way that made sense for **rickroot's** development habits and preferences. This is not a shell built for many; it is a shell built for **one person**.

The fact that others may find some utility in Rickshell is more of an unintended side effect than a goal. This shell is **not** intended to suit the needs of a wide range of developers, and there is **no guarantee** that it will work for you. It was built with the express purpose of being **rickroot's own custom tool** for managing and enhancing their development process.
## Why Build Rickshell?

In the world of software development, the tools we use have a profound impact on our productivity and creativity. For most developers, traditional shells—whether it's Bash, Zsh, or Fish—offer the essential functionality needed to interact with the operating system. They come with a set of pre-built commands, some customization options, and a host of features that have been optimized over decades of use. Yet, for all their strengths, these shells follow a one-size-fits-all philosophy. They aim to cater to the general needs of the average developer, often leaving power users, those with very specific workflows, in search of something more.

**Rickshell** was born out of a deep dissatisfaction with this status quo. It started not as a public project or even a solution to a widely recognized problem, but as a **personal quest** by its creator, **rickroot**, to build a shell that _truly fits their own development needs_. Rickshell is a reflection of a singular vision: to craft a shell environment that is optimized not for the masses, but for one developer’s exact preferences and workflows.

### Personalization at the Core
The philosophy behind Rickshell is simple: **the tools should adapt to the developer, not the other way around**. In traditional shell environments, users often find themselves spending time configuring, extending, and sometimes even fighting against the tool to get it to work the way they want. This inefficiency is what Rickshell seeks to eliminate. It is not about being a better shell for everyone; it is about being the perfect shell for **rickroot**.

In creating Rickshell, the goal wasn’t to solve a problem that the general development community faces. It was, instead, to eliminate the friction in **rickroot’s** own workflow. Whether it's integrating with the custom Python functions that are often required, managing projects efficiently within the shell environment, or implementing streamlined aliases and commands to speed up repetitive tasks, Rickshell aims to provide **complete control and flexibility**.

### A Tool for One, Shared with Many
While Rickshell is a deeply personal project, the decision to share it with the broader community was driven by the realization that **some developers may find value in its approach**. However, this should not be misconstrued as a guarantee that Rickshell will work for everyone. **This is not a mass-market tool**, and there’s no intention to make it one. If Rickshell happens to solve problems for others along the way, then it is an added benefit, but it was never the driving force behind its creation.

Moreover, unlike traditional shells that are packed with universal features, Rickshell is inherently **minimalist** by design. It offers just what is needed and focuses on personal utility, which may mean leaving out certain features that are common elsewhere but unnecessary for rickroot’s workflow. For example, support for custom shell scripting languages—like those found in Bash—is not a priority or even on the roadmap at this stage. Rickshell is focused on Python integration and built-in functionalities that cater specifically to rickroot’s needs.

Rickshell is, above all, a **statement of independence** in a world where so many tools are designed with everyone in mind. It is a reminder that, as developers, we have the power to create exactly what we need—and sometimes, that's all we need.

## Goals
The primary goal of **Rickshell** is not to be a product or tool for the broader development community, but rather to be a **personalized, fully customizable shell** that makes rickroot’s day-to-day work as seamless and efficient as possible.

In traditional shells, customization and scripting capabilities exist but are limited by the language, architecture, and design philosophies of the shell environment. Rickshell, on the other hand, was designed from the ground up to be completely flexible, prioritizing the needs of its sole intended user, **rickroot**. As such, Rickshell integrates deeply with **Python**, allowing custom functionality to be added in a language that rickroot is comfortable with and frequently uses in their development tasks.
### The goals of Rickshell include:
- **Seamless Python Integration**: To allow for effortless addition of new features, commands, and workflows through Python scripts, extending the shell's core functionality without the need for complex custom shell languages.
- **Efficient Project Management**: Many developers handle numerous projects, which can become unwieldy to manage. Rickshell provides built-in project management capabilities, including **project registration, categorization, and tagging** to keep track of large projects efficiently.
- **Customizable Commands and Aliases**: Rickshell’s built-in commands allow rickroot to define new behaviors and aliases for frequently used commands, reducing the need for repetitive typing and manual execution.
- **Personalized Workflow Optimization**: Rickshell is built entirely around the idea of eliminating friction in rickroot’s specific workflow, ensuring that every command, every feature, and every alias is tuned precisely for maximum productivity.
- **Portability (with Limits)**: While the primary development and testing occur in a **Linux** environment (specifically **Ubuntu 24.04 LTS**), the goal is to make the shell buildable on other systems as well, though no guarantees can be made about compatibility or performance on non-Linux platforms.
### What Rickshell _is not_:
- **It is not designed to replace traditional shells** like Bash, Zsh, or Fish. These are powerful tools in their own right, and Rickshell is not a competitor in that space.
- **It is not aimed at supporting advanced shell scripting** languages like Bash’s scripting capabilities. Rickshell focuses on Python integration for extensibility, and there are no plans to introduce a dedicated shell scripting language.
- **It is not a tool for everyone.** Rickshell was built for one person, and it may not fit the needs of other developers. If you try it and it happens to work for you, that’s great! But the primary goal remains to provide a customized experience for rickroot.

## Key Features
- **C-based shell** for performance, combined with Python extensibility for easy customization.
- **Customizable internal commands**, allowing users to define new behaviors not found in traditional shells.
- **Project Management System**: Register, categorize, and manage multiple projects from within the shell.
- **Tagging system**: Add user-defined tags to projects for quick access and filtering.
- **Project categorization**: Organize projects by categories for easier retrieval.
- **User-defined aliases** for quicker execution of frequently used commands.

## Project Status
As of now, **Rickshell is still in active development**. Many of the features described in this document are either in progress or planned for future updates. As such, users who decide to try Rickshell should be aware that **not all functionality may work as expected**, and some features may still have bugs or performance issues.

## Compatibility and Limitations
### Supported Platforms:
- **Primary Focus**: Linux, specifically **Ubuntu 24.04 LTS**
- **Future Considerations**: While **Linux-first** development is the current focus, portability to other platforms is being explored, but there is no guarantee of compatibility or smooth performance on **non-Linux** systems at this time.
### Key Limitations:
- **Linux-Only**: Rickshell is designed for **Linux** and will likely not work on **Windows** or **macOS** without significant modifications.
- **No Mass-Market Support**: Rickshell is **not** intended to replace popular shells like Bash or Zsh and lacks support for advanced shell scripting languages like **Bash**. Python is the core integration language.
## Development Status
Since Rickshell is still in **active development**, it is not yet a fully stable or polished product. Users trying Rickshell should be prepared for potential issues, including:
- **Incomplete Features**: Certain features are still in progress and may not work as expected.
- **Bugs and Performance Issues**: Testing is ongoing, and some commands or features may not yet be optimized for performance.
If you choose to explore Rickshell, we recommend monitoring the [GitHub Issues](https://github.com/rickroot30/rickshell/issues) for updates, bug fixes, and feature requests.

## License
Rickshell is licensed under the [`GNU Affero General Public License v3.0`](LICENSE)