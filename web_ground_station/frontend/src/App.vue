<template>
  <main>
    <h1>NUEDC Ground Station</h1>
    <p>Link: {{ store.commandLink }}</p>
    <p>Mission: {{ store.missionRunning ? 'RUNNING' : 'IDLE' }}</p>
  </main>
</template>

<script setup lang="ts">
import { useTelemetry } from './composables/useTelemetry';
import { useGroundStore } from './stores/ground';

const store = useGroundStore();
useTelemetry();
</script>
