const net = require("net");
const fs = require("fs");

//will resolve into -1 if there was an error or 0 if it all went fine


module.exports.generateTTS = (text, host, port,file) =>
{
return new Promise( (resolve) => {
	try
	{
		fs.unlinkSync(file);
	}catch{};

	const client = net.createConnection(port, host, () => {
		client.write(text);
	});


	client.on("data", (data) => 
	{
		if(data.toString()=="Denied")
		{
			resolve(-1);
		}
		fs.appendFileSync(file, data);
	})


	client.on("error", (error) => {
    		console.log(`TTS ERROR`);
		console.log(error);
		resolve(-1);
	});

	client.on("close", () => {
		resolve(0);
	});
})
}
