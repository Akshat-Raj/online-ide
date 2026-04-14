const express = require('express');
const app = express();
const fs = require('fs');
const path = require('path');
const { execFile } = require('child_process');
const crypto = require('crypto');
const fastq = require('fastq');
const bindings = require('bindings');

const executor = bindings('executor');

app.set('view engine', 'ejs');
app.use(express.json());

const tmpDir = path.join(__dirname, 'tmp');
if (!fs.existsSync(tmpDir)) {
    fs.mkdirSync(tmpDir);
}

function executeTask(task, cb) {
    const { code, input, timeLimit, memoryLimit } = task;
    const id = crypto.randomBytes(8).toString('hex');
    const sourcePath = path.join(tmpDir, `${id}.cpp`);
    const outPath = path.join(tmpDir, `${id}`);
    const inputPath = path.join(tmpDir, `${id}_in.txt`);
    const resultOutPath = path.join(tmpDir, `${id}_out.txt`);

    fs.writeFileSync(sourcePath, code);
    if (input) {
        fs.writeFileSync(inputPath, input);
    } else {
        fs.writeFileSync(inputPath, '');
    }

    execFile('g++', [sourcePath, '-o', outPath, '-O2', '-std=c++17'], (err, stdout, stderr) => {
        if (err) {
            fs.unlinkSync(sourcePath);
            try { fs.unlinkSync(inputPath); } catch(e) {}
            return cb(null, { verdict: 'CE', output: stderr });
        }

        // Call the C++ add-on to execute the compiled binary, passing limits and output file location
        const execResult = executor.executeCode(outPath, timeLimit, memoryLimit, resultOutPath, inputPath);
        const status = execResult.status;
        const timeTaken = execResult.time || 0; // The actual binary runtime computed in executor.cc
        const memoryUsed = (execResult.memory || 0).toFixed(2); // Memory consumed

        // Clean up files
        try { fs.unlinkSync(sourcePath); } catch (e) {}
        try { fs.unlinkSync(outPath); } catch (e) {}
        try { fs.unlinkSync(inputPath); } catch (e) {}
        
        // Read executed output
        let executedOutput = '';
        try { 
            if (fs.existsSync(resultOutPath)) {
                executedOutput = fs.readFileSync(resultOutPath, 'utf8');
                fs.unlinkSync(resultOutPath); 
            }
        } catch (e) {}

        // status mapping: 1: OK, 2: TLE, 3: MLE/RE, 4: RE
        if (status === 2) {
             return cb(null, { verdict: 'TLE', time: timeTaken, memory: memoryUsed, exitCode: -1, submittedCode: code, output: 'Time limit exceeded.' });
        } else if (status === 3 || status === 4) {
             return cb(null, { verdict: 'RE', output: executedOutput || 'Runtime Error / Memory Limit Exceeded', time: timeTaken, memory: memoryUsed, exitCode: -1, submittedCode: code });
        }

        cb(null, { verdict: 'OK', output: executedOutput, time: timeTaken, memory: memoryUsed, exitCode: 0, submittedCode: code });
    });
}

const queue = fastq(executeTask, 1); // concurrency 1

app.get('/', (req, res) => {
    res.render('index',{text:'hello world'})
})

app.post('/execute', (req, res) => {
    const { code, input, timeLimit, memoryLimit } = req.body;
    
    queue.push({ code, input, timeLimit: timeLimit || 1000, memoryLimit: memoryLimit || 256 }, (err, result) => {
        if (err) {
            return res.status(500).json({ verdict: 'RE', output: 'Internal Server Error' });
        }
        res.json(result);
    });
})

const PORT = process.env.PORT || 3000;
app.listen(PORT, '0.0.0.0', () => {
    console.log(`Server running on port ${PORT}`);
});