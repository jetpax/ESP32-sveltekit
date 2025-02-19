import type { PageLoad } from './$types';

export const load = (async ({ fetch }) => {
	return {
		title: 'Berry REPL'
	};
}) satisfies PageLoad;
