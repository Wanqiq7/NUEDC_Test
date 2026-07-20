import '@quasar/extras/material-icons/material-icons.css';
import 'quasar/src/css/index.sass';

import { createPinia } from 'pinia';
import { Quasar } from 'quasar';
import { createApp } from 'vue';

import App from './App.vue';

createApp(App).use(createPinia()).use(Quasar, { plugins: {} }).mount('#app');
