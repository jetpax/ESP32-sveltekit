/** @type {import('tailwindcss').Config} */
module.exports = {
	content: ['./src/**/*.{html,js,svelte,ts}'],
	theme: {
		extend: {
      fontWeight: {
        bold: '600', 
      },
    },
	},
	plugins: [require('daisyui')],
	daisyui: {
		themes: ['corporate', 'business'],
		darkTheme: 'business'
	}
};
