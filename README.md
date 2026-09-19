# Canvas Assignment Tracker

A small C web app built with the Mongoose framework. It connects to Canvas, lets a student choose a course, and displays that course's assignments in a readable table.

## Setup

1. Install [MSYS2](https://www.msys2.org/) and open the **MSYS2 UCRT64** terminal.
2. Open the MSYS2 terminal in the folder where you cloned this repository. Alternatively, navigate to your own copy with `cd /path/to/your/repository`.

3. Copy `.env.example` to `.env` and add your Canvas access token:

	`CANVAS_API_TOKEN=your_token_here`

4. Build the app:

	`gcc -DMG_TLS=3 -o server.exe main.c mongoose.c -lws2_32`

5. Start it:

	`./server.exe`

6. Open <http://localhost:8080>, choose a course, and click **Show assignments**.

Never commit or share `.env`. If a token is exposed, revoke it in Canvas and create a new one.

## Canvas API Endpoints

| Endpoint | Purpose |
| --- | --- |
| `GET /api/v1/courses` | Gets the student's active courses. |
| `GET /api/v1/courses/:course_id/assignments` | Gets assignments for the selected course. |

Both requests use bearer-token authentication. The app follows Canvas `Link` headers with `rel="next"` to handle pagination.

## Features

- Course selection through an HTML form.
- Assignment names, due dates, points, links, and submission status.
- Bootstrap formatting instead of raw JSON output.
- Friendly messages for missing tokens, network failures, and Canvas errors.

## Reflection

This project taught me how to authenticate with a REST API using a bearer token and how to parse JSON in C. The most challenging part was handling asynchronous HTTPS requests and Canvas pagination through the `Link` response header.

With more time, I would add tests, caching, and a page showing upcoming assignments across all courses. I would also use OAuth so users would not need to manage personal access tokens manually.