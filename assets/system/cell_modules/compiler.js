/**
 *  Cell Standard Library 'compiler' module
 *  (c) 2017 Fat Cerberus
**/

exports.__esModule = true;
const Babel = require('#/babel-core');

const ModuleTool = makeTranspileTool(2.0);
const ScriptTool = makeTranspileTool(1.0);

exports.transpile = transpile;
function transpile(dirName, sources)
{
	return stageTranspileJob(dirName, sources);
}

function makeTranspileTool(apiVersion)
{
	return new Tool(function(outFileName, inFileNames) {
		var moduleType = apiVersion >= 2.0 ? 'commonjs' : false;
		var sourceType = apiVersion >= 2.0 ? 'module' : 'script';
		var fileContent = FS.readFile(inFileNames[0]);
		var input = new TextDecoder().decode(fileContent);
		var output = Babel.transform(input, {
			sourceType,
			comments: false,
			retainLines: true,
			presets: [
				[ 'latest', { es2015: { modules: moduleType } } ]
			]
		});
		FS.writeFile(outFileName, new TextEncoder().encode(output.code));
	}, "transpiling");
}

function stageTranspileJob(dirName, sources)
{
	var targets = [];
	FS.createDirectory(dirName);
	for (var i = 0; i < sources.length; ++i) {
		var fileName = FS.resolve(dirName + '/' + sources[i].name);
		var tool = fileName.endsWith('.mjs') ? ModuleTool : ScriptTool;
		var target = tool.stage(fileName, [ sources[i] ], {
			name: sources[i].name,
		});
		targets[targets.length] = target;
	}
	return targets;
}