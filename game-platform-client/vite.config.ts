import { defineConfig, loadEnv } from 'vite'
import vue from '@vitejs/plugin-vue'
import { resolve } from 'path'
import { VitePWA } from 'vite-plugin-pwa'

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '')
  const port = parseInt(env.VITE_PORT || '3001', 10)
  // 代理目标：直连模式用 SERVICE_HOST，代理模式用 API_BASE_URL
  const proxyTarget = env.VITE_USE_DIRECT_CONNECTION === 'true'
    ? `http://${env.VITE_SERVICE_HOST || '127.0.0.1'}`
    : (env.VITE_API_BASE_URL || 'http://127.0.0.1')
  const wsProxyTarget = proxyTarget.replace(/^http/, 'ws')

  return {
    plugins: [
      vue(),
      VitePWA({
        registerType: 'autoUpdate',
        includeAssets: ['favicon.ico', 'pwa-192x192.png', 'pwa-512x512.png'],
        manifest: {
          name: 'Game Platform',
          short_name: 'GamePlatform',
          description: 'Online Game Platform',
          theme_color: '#4f46e5',
          background_color: '#ffffff',
          display: 'standalone',
          icons: [
            {
              src: 'pwa-192x192.png',
              sizes: '192x192',
              type: 'image/png',
            },
            {
              src: 'pwa-512x512.png',
              sizes: '512x512',
              type: 'image/png',
            },
          ],
        },
      }),
    ],
    resolve: {
      alias: {
        '@': resolve(__dirname, 'src'),
      },
    },
    server: {
      port,
      proxy: {
        '/api': {
          target: proxyTarget,
          changeOrigin: true,
        },
        '/ws': {
          target: wsProxyTarget,
          ws: true,
          changeOrigin: true,
        },
      },
    },
    build: {
      target: 'es2015',
      outDir: 'dist',
      sourcemap: false,
      rollupOptions: {
        output: {
          manualChunks: {
            'vue-vendor': ['vue', 'vue-router', 'pinia'],
            'element-plus': ['element-plus', '@element-plus/icons-vue'],
          },
        },
      },
      chunkSizeWarningLimit: 1000,
    },
  }
})
