FROM node:18-bullseye

# Install g++ and Python (required for node-gyp native compilation)
RUN apt-get update && \
    apt-get install -y g++ make python3 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy package files and binding configuration
COPY package*.json ./
COPY binding.gyp ./

# Install dependencies and build native N-API module
RUN npm install && npm run build:native || node-gyp rebuild

# Copy remaining source code
COPY . .

EXPOSE 3000

# Start server
CMD ["node", "server.js"]
