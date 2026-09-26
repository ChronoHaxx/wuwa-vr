# Help make WuWa VR more welcoming

This is a free, unofficial fan project. You do not need to donate, write code
or speak English to contribute. Clear bug reports, translations, comfort
feedback and small fixes all help. Volunteers are welcome; response times and
continued maintenance are not guaranteed.

## Report something or request a language

Use **Feedback** on the website for a bug, feature, accessibility or language
request. Describe the problem in the language you prefer. The form prepares a
GitHub issue for you to review; it never posts automatically. GitHub sign-in
is needed to submit there. You can also copy the report as plain text.

Until the owner connects the public repository, the website clearly offers
copy-only drafts. It does not pretend your request has been sent. On GitHub,
choose **New issue** and the matching template. Do not include account IDs,
personal paths, login details or unreviewed logs. A short description and the
build/runtime are usually enough to start.

## Translations

Getting started, common Xbox shortcuts, feedback and support expectations are
available in English, Simplified Chinese, Japanese, Korean, Spanish, Brazilian
Portuguese, French, German, Russian and Arabic. These are initial translations
and community language review is welcome. The full technical guide and native
UEVR interface remain English; game language is not changed.

Edit the appropriate `site/languages/*.js` source and run
`node dev/build-community.cjs`. Each translated phrase has the same key as the
English reference. Keep exact in-game option names and Xbox button names
unchanged so players can find them. Do not add a remote translation service.
Right-to-left languages use `dir="rtl"`; controller chords remain left-to-right.
Test keyboard use, small screens, long labels and the copy/report flow.

## Code, forks and sharing

Keep a fix small, describe the actual problem and include how you checked it.
Distinguish offline tests from game/headset acceptance. Preserve existing
attribution. The [scoped license](LICENSE.md) identifies original MIT work;
do not relicense upstream code or publish private runtime/game data.

Fork and improve the MIT-covered parts freely. Please keep community access
free, share improvements and avoid sold repacks or paywalled forks. That is
our community preference, not a restriction added to MIT: commercial use is
allowed by MIT. Public combined-mod redistribution still needs the upstream
scope clarified. Nothing in this project requires payment to report a bug,
ask for a language or contribute.
