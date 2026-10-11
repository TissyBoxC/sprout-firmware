# English Course

Caches a bounded English lesson catalog and a bounded local practice log so the
device can run the English learning flow offline and stay consistent with the
parent app's English learning view (`/api/v1/english/lessons` and
`/api/v1/english/progress`).

Each lesson keeps a stable lesson id, title, age tier, and its ordered words
(term, phonetic, translation, example) and sentence patterns (pattern, prompt,
model answer). A practice attempt keeps a lesson id, an item id, the item type
(`word`, `sentence`), the clamped pronunciation score, the derived pass flag,
and the epoch-seconds time it happened. The module never stores audio, content
bytes, conversation content, credentials, or raw upstream data.

## Contract

- The cache holds at most `ENGLISH_COURSE_LESSON_CAPACITY` (12) lessons, each
  with at most `ENGLISH_COURSE_WORDS_PER_LESSON` (8) words and
  `ENGLISH_COURSE_SENTENCES_PER_LESSON` (4) sentences.
- The practice log holds at most `ENGLISH_COURSE_ATTEMPT_CAPACITY` (32)
  attempts; appending past the limit drops the oldest one.
- A score is clamped to 0-100 and `passed` is derived from
  `ENGLISH_COURSE_PASSING_SCORE` (60) so a caller cannot claim a pass it did not
  earn.
- `english_course_lesson_progress` rolls up the best score, attempt count, and
  passed flag per distinct item for one lesson.
- The cache persists as one binary blob in `config_store`; a missing or corrupt
  blob starts from an empty, valid cache so startup never fails.

## Behavior

`english_course_upsert_lesson`, `english_course_remove_lesson`,
`english_course_record_attempt`, and `english_course_clear_attempts` mutate the
in-memory cache and persist it. `english_course_list_lessons`,
`english_course_get_lesson`, `english_course_list_attempts`, and
`english_course_lesson_progress` let the lesson and practice paths read the
cached catalog, history, and progress.
